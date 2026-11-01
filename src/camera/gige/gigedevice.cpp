#include "camera/gige/gigedevice.h"

#include "camera/gige/gvcp.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace calib
{

namespace
{

constexpr std::uint32_t kHeartbeatTimeoutMs = 3000;
constexpr auto kHeartbeatInterval = std::chrono::milliseconds(1000);
constexpr int kMaxFailedHeartbeats = 3;
constexpr std::uint32_t kPacketSize = 1500;
constexpr std::uint32_t kFireTestPacket = 0x80000000;
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
    if(!configureStream(camera.hostIp, &why))
    {
        return failWith(why);
    }

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

        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(std::min(deadline, nextHeartbeat)
                                                                                - now);
        std::uint32_t fromIp = 0;
        const int received = stream.receive(packet.data(), packet.size(), static_cast<int>(wait.count()) + 1, &fromIp);
        if(received < 0)
        {
            lastError = "stream socket error";
            return false;
        }
        if(received == 0 || fromIp != deviceIp)
        {
            continue;
        }

        if(assembler.push(packet.data(), static_cast<std::size_t>(received)) == gvsp::FrameAssembler::Result::Frame)
        {
            if(gvsp::toMat(assembler.getPixelFormat(), assembler.getWidth(), assembler.getHeight(),
                           assembler.getImage().data(), assembler.getImageSize(), &frame, &lastError))
            {
                return true;
            }
            return false;
        }
    }
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

bool GigEDevice::configureStream(std::uint32_t hostIp, std::string* error)
{
    if(!stream.open(hostIp, 0, error))
    {
        return false;
    }
    stream.setReceiveBufferSize(kReceiveBufferBytes);

    std::string why;
    if(!control.writeRegister(gvcp::reg::kStreamChannelDestination0, hostIp, &why))
    {
        *error = "cannot set the stream destination: " + why;
        return false;
    }

    std::uint32_t scps = 0;
    if(control.readRegister(gvcp::reg::kStreamChannelPacketSize0, &scps))
    {
        const std::uint32_t flags = scps & ~gvcp::kPacketSizeMask & ~kFireTestPacket;
        control.writeRegister(gvcp::reg::kStreamChannelPacketSize0, flags | kPacketSize);
        control.readRegister(gvcp::reg::kStreamChannelPacketSize0, &scps);
    }
    packetSize = scps & gvcp::kPacketSizeMask;
    if(packetSize == 0)
    {
        packetSize = kPacketSize;
    }
    assembler.setPacketSize(packetSize);

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
    return true;
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
