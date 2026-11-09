#include "camera/gige/gigedevice.h"

#include "camera/gige/gvcp.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace calib
{

namespace
{

constexpr std::uint32_t kHeartbeatTimeoutMs = 3000;
constexpr auto kHeartbeatInterval = std::chrono::milliseconds(1000);
constexpr int kMaxFailedHeartbeats = 3;
constexpr std::uint32_t kPacketSize = 1500;
constexpr std::uint32_t kMinPacketSize = 576;
constexpr std::uint32_t kMaxPacketSize = 9000;
constexpr std::uint32_t kFireTestPacket = 0x80000000;
constexpr std::uint32_t kDoNotFragment = 0x40000000;
constexpr int kTestPacketWaitMs = 300;
constexpr int kReceivePollMs = 20;
constexpr int kReceiveBufferBytes = 32 * 1024 * 1024;
constexpr std::size_t kMaxXmlFileSize = 16 * 1024 * 1024;

bool endsWithIgnoringCase(const std::string& text, const std::string& suffix)
{
    if(text.size() < suffix.size())
    {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

void raiseThreadPriority(void)
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

} // namespace


// GenICam register access over GVCP: READREG/WRITEREG for aligned 32-bit
// registers, READMEM/WRITEMEM for everything else.
class GigEDevice::ControlPort final : public IRegisterPort
{
private:
    GvcpClient& control;

public:
    explicit ControlPort(GvcpClient& control)
        : control(control)
    {
    }

    bool read(std::uint64_t address, std::uint8_t* data, std::size_t size) override
    {
        const auto start = static_cast<std::uint32_t>(address);
        if(size == 4 && start % 4 == 0)
        {
            std::uint32_t value = 0;
            if(!control.readRegister(start, &value))
            {
                return false;
            }
            gvcp::writeBe32(data, value);
            return true;
        }
        const std::uint32_t alignedStart = start & ~3u;
        const std::size_t alignedSize = ((start + size + 3) & ~std::size_t(3)) - alignedStart;
        std::vector<std::uint8_t> buffer(alignedSize);
        if(!control.readMemory(alignedStart, buffer.data(), alignedSize))
        {
            return false;
        }
        std::memcpy(data, buffer.data() + (start - alignedStart), size);
        return true;
    }

    bool write(std::uint64_t address, const std::uint8_t* data, std::size_t size) override
    {
        const auto start = static_cast<std::uint32_t>(address);
        if(size == 4 && start % 4 == 0)
        {
            return control.writeRegister(start, gvcp::readBe32(data));
        }
        if(size % 4 == 0 && start % 4 == 0)
        {
            return control.writeMemory(start, data, size);
        }
        const std::uint32_t alignedStart = start & ~3u;
        const std::size_t alignedSize = ((start + size + 3) & ~std::size_t(3)) - alignedStart;
        std::vector<std::uint8_t> buffer(alignedSize);
        if(!control.readMemory(alignedStart, buffer.data(), alignedSize))
        {
            return false;
        }
        std::memcpy(buffer.data() + (start - alignedStart), data, size);
        return control.writeMemory(alignedStart, buffer.data(), alignedSize);
    }
};


GigEDevice::GigEDevice(void)
    : packet(16 * 1024)
{
}

GigEDevice::~GigEDevice(void)
{
    close();
}

bool GigEDevice::open(const CameraInfo& camera, std::string* error)
{
    close();
    const auto failWith = [this, error](const std::string& message)
    {
        if(error)
        {
            *error = message;
        }
        close();
        return false;
    };

    if(camera.deviceIp == 0 || camera.hostIp == 0)
    {
        return failWith("the camera's network address is unknown");
    }
    deviceIp = camera.deviceIp;

    std::string why;
    if(!control.connect(camera.deviceIp, camera.hostIp, &why))
    {
        return failWith(why);
    }

    if(!control.writeRegister(gvcp::reg::kControlChannelPrivilege, gvcp::kPrivilegeControl, &why))
    {
        if(why.rfind("camera answered", 0) == 0)
        {
            return failWith("the camera refused control (" + why
                            + "); close other camera software and try again in a few seconds");
        }
        return failWith("the camera does not answer at " + gvcp::ipv4ToString(camera.deviceIp)
                        + " (check its power and cable)");
    }
    connected = true;
    control.writeRegister(gvcp::reg::kHeartbeatTimeout, kHeartbeatTimeoutMs);
    nextHeartbeat = std::chrono::steady_clock::now() + kHeartbeatInterval;
    failedHeartbeats = 0;

    if(!loadNodeMap(&why))
    {
        return failWith(why);
    }
    if(!configureStream(camera, &why))
    {
        return failWith(why);
    }

    startReceiver();
    prepareAcquisition();
    if(nodes.has("TLParamsLocked"))
    {
        nodes.setInteger("TLParamsLocked", 1);
    }
    if(!nodes.execute("AcquisitionStart"))
    {
        return failWith("AcquisitionStart failed: " + nodes.getLastError());
    }
    acquiring = true;
    opened = true;
    return true;
}

void GigEDevice::close(void)
{
    stopReceiver();
    if(connected && failedHeartbeats < kMaxFailedHeartbeats)
    {
        if(acquiring)
        {
            nodes.execute("AcquisitionStop");
            if(nodes.has("TLParamsLocked"))
            {
                nodes.setInteger("TLParamsLocked", 0);
            }
        }
        control.writeRegister(gvcp::reg::kStreamChannelPort0, 0);
        control.writeRegister(gvcp::reg::kControlChannelPrivilege, 0);
    }
    acquiring = false;
    opened = false;
    connected = false;
    streamSourcePort = 0;
    nodes.setPort(nullptr);
    port.reset();
    control.disconnect();
    stream.close();
    assembler.reset();
}

bool GigEDevice::read(cv::Mat& frame, int timeoutMs)
{
    if(!opened)
    {
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while(true)
    {
        const auto now = Clock::now();
        if(now >= nextHeartbeat && !keepAlive())
        {
            lastError = "the camera stopped answering (cable unplugged or camera restarted?)";
            opened = false;
            return false;
        }
        if(now >= deadline)
        {
            lastError = "no image within " + std::to_string(timeoutMs) + " ms";
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(frameMutex);
            frameArrived.wait_until(lock, std::min(deadline, nextHeartbeat),
                                    [this]() { return hasReady || streamFailed.load(); });
            if(!hasReady)
            {
                if(streamFailed.load())
                {
                    lastError = "stream socket error";
                    return false;
                }
                continue;
            }
            std::swap(current, ready);
            hasReady = false;
        }

        return gvsp::toMat(current.pixelFormat, current.width, current.height, current.data.data(), current.size,
                           &frame, &lastError);
    }
}

void GigEDevice::startReceiver(void)
{
    stopReceiver();
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        hasReady = false;
        streamFailed = false;
    }
    receiving = true;
    receiver = std::thread(&GigEDevice::receiveLoop, this);
}

void GigEDevice::stopReceiver(void)
{
    receiving = false;
    if(receiver.joinable())
    {
        receiver.join();
    }
}

void GigEDevice::receiveLoop(void)
{
    raiseThreadPriority();
    while(receiving.load(std::memory_order_relaxed))
    {
        std::uint32_t fromIp = 0;
        const int received = stream.receive(packet.data(), packet.size(), kReceivePollMs, &fromIp);
        if(received < 0)
        {
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                streamFailed = true;
            }
            frameArrived.notify_all();
            return;
        }
        if(received == 0 || fromIp != deviceIp)
        {
            continue;
        }
        if(assembler.push(packet.data(), static_cast<std::size_t>(received)) == gvsp::FrameAssembler::Result::Frame)
        {
            publishAssembledFrame();
        }
    }
}

void GigEDevice::publishAssembledFrame(void)
{
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        assembler.swapImage(ready.data);
        ready.size = assembler.getImageSize();
        ready.pixelFormat = assembler.getPixelFormat();
        ready.width = assembler.getWidth();
        ready.height = assembler.getHeight();
        hasReady = true;
    }
    frameArrived.notify_one();
}

bool GigEDevice::loadNodeMap(std::string* error)
{
    char url[gvcp::reg::kUrlSize + 1] = {};
    if(!control.readMemory(gvcp::reg::kFirstUrl, url, gvcp::reg::kUrlSize, error))
    {
        return false;
    }

    std::string fileName;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    if(!parseGenICamUrl(url, &fileName, &address, &size, error))
    {
        return false;
    }
    if(size == 0 || size > kMaxXmlFileSize)
    {
        *error = "implausible GenICam file size " + std::to_string(size);
        return false;
    }

    std::string file(static_cast<std::size_t>(size), '\0');
    if(!control.readMemory(static_cast<std::uint32_t>(address), &file[0], file.size(), error))
    {
        *error = "cannot download the GenICam file: " + *error;
        return false;
    }

    std::string xml;
    if(endsWithIgnoringCase(fileName, ".zip"))
    {
        if(!extractXmlFromZip(file, &xml, error))
        {
            return false;
        }
    }
    else
    {
        xml = std::move(file);
        xml.resize(std::strlen(xml.c_str()));
    }

    if(!nodes.load(xml, error))
    {
        return false;
    }
    port = std::make_unique<ControlPort>(control);
    nodes.setPort(port.get());
    return true;
}

bool GigEDevice::configureStream(const CameraInfo& camera, std::string* error)
{
    if(!stream.open(camera.hostIp, 0, error))
    {
        return false;
    }
    stream.setReceiveBufferSize(kReceiveBufferBytes);

    std::string why;
    if(!control.writeRegister(gvcp::reg::kStreamChannelDestination0, camera.hostIp, &why))
    {
        *error = "cannot set the stream destination: " + why;
        return false;
    }
    if(!control.writeRegister(gvcp::reg::kStreamChannelPort0, stream.getLocalPort(), &why))
    {
        *error = "cannot open the stream channel: " + why;
        return false;
    }

    std::uint32_t sourcePort = 0;
    if(control.readRegister(gvcp::reg::kStreamChannelSourcePort0, &sourcePort))
    {
        streamSourcePort = static_cast<std::uint16_t>(sourcePort & 0xFFFF);
    }
    openFirewallPath();

    packetSize = negotiatePacketSize(camera.hostMtu);
    assembler.setPacketSize(packetSize);
    control.writeRegister(gvcp::reg::kStreamChannelPacketDelay0, 0);
    return true;
}

std::uint32_t GigEDevice::negotiatePacketSize(std::uint32_t hostMtu)
{
    std::uint32_t scps = 0;
    if(!control.readRegister(gvcp::reg::kStreamChannelPacketSize0, &scps))
    {
        return kPacketSize;
    }
    const std::uint32_t flags = scps & ~gvcp::kPacketSizeMask & ~kFireTestPacket;

    std::uint32_t wanted = kPacketSize;
    if(hostMtu >= kMinPacketSize && hostMtu < kPacketSize)
    {
        wanted = hostMtu & ~3u;
    }
    else if(hostMtu > kPacketSize)
    {
        std::uint32_t jumbo = std::min(hostMtu, kMaxPacketSize);
        double minimum = 0.0;
        double maximum = 0.0;
        if(nodes.has("GevSCPSPacketSize") && nodes.getRange("GevSCPSPacketSize", &minimum, &maximum)
           && maximum >= kPacketSize)
        {
            jumbo = std::min(jumbo, static_cast<std::uint32_t>(maximum));
        }
        jumbo &= ~7u;
        if(jumbo > kPacketSize && packetSizeWorks(flags, &jumbo))
        {
            wanted = jumbo;
        }
    }

    control.writeRegister(gvcp::reg::kStreamChannelPacketSize0, flags | wanted);
    if(control.readRegister(gvcp::reg::kStreamChannelPacketSize0, &scps) && (scps & gvcp::kPacketSizeMask) != 0)
    {
        return scps & gvcp::kPacketSizeMask;
    }
    return wanted;
}

bool GigEDevice::packetSizeWorks(std::uint32_t flags, std::uint32_t* size)
{
    if(!control.writeRegister(gvcp::reg::kStreamChannelPacketSize0,
                              flags | kFireTestPacket | kDoNotFragment | *size))
    {
        return false;
    }
    std::uint32_t scps = 0;
    if(control.readRegister(gvcp::reg::kStreamChannelPacketSize0, &scps))
    {
        const std::uint32_t accepted = scps & gvcp::kPacketSizeMask;
        if(accepted <= kPacketSize)
        {
            return false;
        }
        *size = accepted;
    }

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(kTestPacketWaitMs);
    for(auto now = Clock::now(); now < deadline; now = Clock::now())
    {
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::uint32_t fromIp = 0;
        const int received = stream.receive(packet.data(), packet.size(), static_cast<int>(wait.count()) + 1, &fromIp);
        if(received < 0)
        {
            return false;
        }
        if(fromIp == deviceIp && static_cast<std::size_t>(received) + gvsp::kIpUdpOverhead > kPacketSize)
        {
            return true;
        }
    }
    return false;
}

void GigEDevice::openFirewallPath(void)
{
    if(streamSourcePort != 0)
    {
        const std::uint8_t probe[4] = {};
        stream.sendTo(probe, sizeof(probe), deviceIp, streamSourcePort);
    }
}

void GigEDevice::prepareAcquisition(void)
{
    if(nodes.has("AcquisitionMode"))
    {
        nodes.setEnumeration("AcquisitionMode", "Continuous");
    }
    if(nodes.has("TriggerSelector"))
    {
        nodes.setEnumeration("TriggerSelector", "FrameStart");
    }
    if(nodes.has("TriggerMode"))
    {
        nodes.setEnumeration("TriggerMode", "Off");
    }
}

bool GigEDevice::keepAlive(void)
{
    std::uint32_t privilege = 0;
    if(control.readRegister(gvcp::reg::kControlChannelPrivilege, &privilege))
    {
        openFirewallPath();
        failedHeartbeats = 0;
        nextHeartbeat = std::chrono::steady_clock::now() + kHeartbeatInterval;
        return (privilege & (gvcp::kPrivilegeControl | gvcp::kPrivilegeExclusive)) != 0;
    }
    ++failedHeartbeats;
    nextHeartbeat = std::chrono::steady_clock::now() + kHeartbeatInterval / 4;
    return failedHeartbeats < kMaxFailedHeartbeats;
}


} // namespace calib
