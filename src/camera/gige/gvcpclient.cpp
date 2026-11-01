#include "camera/gige/gvcpclient.h"

#include "camera/gige/gvcp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace calib
{

namespace
{

void setError(std::string* error, const std::string& message)
{
    if(error)
    {
        *error = message;
    }
}

std::string hex(std::uint32_t value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return buffer;
}

} // namespace

bool GvcpClient::connect(std::uint32_t deviceIp, std::uint32_t hostIp, std::string* error)
{
    this->deviceIp = deviceIp;
    reply.resize(2048);
    return socket.open(hostIp, 0, error);
}

void GvcpClient::disconnect(void)
{
    socket.close();
}

std::uint16_t GvcpClient::nextRequestId(void)
{
    requestId = static_cast<std::uint16_t>(requestId == 0xFFFF ? 1 : requestId + 1);
    return requestId;
}

bool GvcpClient::readRegister(std::uint32_t address, std::uint32_t* value, std::string* error)
{
    std::vector<std::uint8_t> payload;
    if(!transact(gvcp::makeReadRegCommand(nextRequestId(), {address}), gvcp::kReadRegAck, &payload, error))
    {
        return false;
    }
    if(payload.size() < 4)
    {
        setError(error, "short READREG reply for " + hex(address));
        return false;
    }
    *value = gvcp::readBe32(payload.data());
    return true;
}

bool GvcpClient::writeRegister(std::uint32_t address, std::uint32_t value, std::string* error)
{
    std::vector<std::uint8_t> payload;
    return transact(gvcp::makeWriteRegCommand(nextRequestId(),
                                              {
                                                  {address, value}
    }),
                    gvcp::kWriteRegAck, &payload, error);
}

bool GvcpClient::readMemory(std::uint32_t address, void* data, std::size_t size, std::string* error)
{
    auto* out = static_cast<std::uint8_t*>(data);
    std::vector<std::uint8_t> payload;
    std::size_t done = 0;
    while(done < size)
    {
        const std::size_t wanted = std::min(gvcp::kMaxMemoryChunk, size - done);
        const auto count = static_cast<std::uint16_t>((wanted + 3) & ~std::size_t(3));
        const auto chunkAddress = static_cast<std::uint32_t>(address + done);
        if(!transact(gvcp::makeReadMemCommand(nextRequestId(), chunkAddress, count), gvcp::kReadMemAck, &payload,
                     error))
        {
            return false;
        }
        if(payload.size() < 4 + wanted)
        {
            setError(error, "short READMEM reply at " + hex(chunkAddress));
            return false;
        }
        std::memcpy(out + done, payload.data() + 4, wanted);
        done += wanted;
    }
    return true;
}

bool GvcpClient::writeMemory(std::uint32_t address, const void* data, std::size_t size, std::string* error)
{
    if(size % 4 != 0)
    {
        setError(error, "WRITEMEM needs a multiple of 4 bytes");
        return false;
    }
    const auto* in = static_cast<const std::uint8_t*>(data);
    std::vector<std::uint8_t> payload;
    for(std::size_t done = 0; done < size; done += gvcp::kMaxMemoryChunk)
    {
        const std::size_t chunk = std::min(gvcp::kMaxMemoryChunk, size - done);
        if(!transact(
               gvcp::makeWriteMemCommand(nextRequestId(), static_cast<std::uint32_t>(address + done), in + done, chunk),
               gvcp::kWriteMemAck, &payload, error))
        {
            return false;
        }
    }
    return true;
}

bool GvcpClient::transact(const std::vector<std::uint8_t>& command, std::uint16_t expectedAck,
                          std::vector<std::uint8_t>* payload, std::string* error)
{
    if(!socket.isOpen())
    {
        setError(error, "control channel is not open");
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const std::uint16_t id = gvcp::readBe16(command.data() + 6);

    for(int attempt = 0; attempt < retries; ++attempt)
    {
        if(!socket.sendTo(command.data(), command.size(), deviceIp, gvcp::kPort))
        {
            setError(error, "cannot send to the camera");
            return false;
        }

        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while(Clock::now() < deadline)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            const int received = socket.receive(reply.data(), reply.size(), static_cast<int>(left.count()) + 1);
            if(received < 0)
            {
                setError(error, "control channel receive error");
                return false;
            }
            gvcp::AckHeader header;
            if(received == 0 || !gvcp::parseAckHeader(reply.data(), static_cast<std::size_t>(received), &header)
               || header.ackId != id)
            {
                continue;
            }

            const std::uint8_t* body = reply.data() + gvcp::kHeaderSize;
            const std::size_t bodySize = std::min<std::size_t>(header.length,
                                                               static_cast<std::size_t>(received) - gvcp::kHeaderSize);
            if(header.answer == gvcp::kPendingAck && bodySize >= 4)
            {
                deadline = Clock::now() + std::chrono::milliseconds(gvcp::readBe16(body + 2) + timeoutMs);
                continue;
            }
            if(header.answer != expectedAck)
            {
                continue;
            }
            if(header.status != gvcp::kStatusSuccess)
            {
                setError(error, std::string("camera answered: ") + gvcp::statusText(header.status));
                return false;
            }
            payload->assign(body, body + bodySize);
            return true;
        }
    }

    setError(error, "the camera does not answer on the control channel");
    return false;
}


} // namespace calib
