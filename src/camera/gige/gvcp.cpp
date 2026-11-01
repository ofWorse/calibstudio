#include "camera/gige/gvcp.h"

#include <cstdio>
#include <utility>

namespace calib::gvcp
{

namespace
{

constexpr std::size_t kVersion = 0x00;
constexpr std::size_t kMacHigh = 0x08;
constexpr std::size_t kMacLow = 0x0C;
constexpr std::size_t kCurrentIp = 0x24;
constexpr std::size_t kCurrentSubnetMask = 0x34;
constexpr std::size_t kCurrentGateway = 0x44;
constexpr std::size_t kManufacturerName = 0x48;
constexpr std::size_t kModelName = 0x68;
constexpr std::size_t kDeviceVersion = 0x88;
constexpr std::size_t kManufacturerInfo = 0xA8;
constexpr std::size_t kSerialNumber = 0xD8;
constexpr std::size_t kUserDefinedName = 0xE8;

std::string readFixedString(const std::uint8_t* p, std::size_t fieldSize)
{
    std::size_t length = 0;

    while(length < fieldSize && p[length] != 0)
    {
        ++length;
    }

    std::string text(reinterpret_cast<const char*>(p), length);

    while(!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.pop_back();
    }

    return text;
}

std::vector<std::uint8_t> makeCommand(std::uint8_t flags, std::uint16_t command, std::uint16_t requestId,
                                      std::size_t payloadSize)
{
    std::vector<std::uint8_t> packet(kHeaderSize + payloadSize, 0);
    packet[0] = kKey;
    packet[1] = flags;
    writeBe16(&packet[2], command);
    writeBe16(&packet[4], static_cast<std::uint16_t>(payloadSize));
    writeBe16(&packet[6], requestId);

    return packet;
}

} // namespace

std::vector<std::uint8_t> makeDiscoveryCommand(std::uint16_t requestId, bool allowBroadcastAck)
{
    const auto flags = static_cast<std::uint8_t>(kFlagAckRequired | (allowBroadcastAck ? kFlagAllowBroadcastAck : 0));

    return makeCommand(flags, kDiscoveryCmd, requestId, 0);
}

std::vector<std::uint8_t> makeForceIpCommand(std::uint16_t requestId, const std::array<std::uint8_t, 6>& mac,
                                             std::uint32_t ip, std::uint32_t subnetMask, std::uint32_t gateway)
{
    std::vector<std::uint8_t> packet = makeCommand(kFlagAckRequired, kForceIpCmd, requestId, kForceIpPayloadSize);
    std::uint8_t* p = packet.data() + kHeaderSize;
    p[0x02] = mac[0];
    p[0x03] = mac[1];
    p[0x04] = mac[2];
    p[0x05] = mac[3];
    p[0x06] = mac[4];
    p[0x07] = mac[5];
    writeBe32(p + 0x14, ip);
    writeBe32(p + 0x24, subnetMask);
    writeBe32(p + 0x34, gateway);

    return packet;
}

std::vector<std::uint8_t> makeReadRegCommand(std::uint16_t requestId, const std::vector<std::uint32_t>& addresses)
{
    std::vector<std::uint8_t> packet = makeCommand(kFlagAckRequired, kReadRegCmd, requestId, 4 * addresses.size());
    for(std::size_t i = 0; i < addresses.size(); ++i)
    {
        writeBe32(&packet[kHeaderSize + 4 * i], addresses[i]);
    }

    return packet;
}

std::vector<std::uint8_t> makeWriteRegCommand(std::uint16_t requestId,
                                              const std::vector<std::pair<std::uint32_t, std::uint32_t>>& writes)
{
    std::vector<std::uint8_t> packet = makeCommand(kFlagAckRequired, kWriteRegCmd, requestId, 8 * writes.size());
    for(std::size_t i = 0; i < writes.size(); ++i)
    {
        writeBe32(&packet[kHeaderSize + 8 * i], writes[i].first);
        writeBe32(&packet[kHeaderSize + 8 * i + 4], writes[i].second);
    }

    return packet;
}

std::vector<std::uint8_t> makeReadMemCommand(std::uint16_t requestId, std::uint32_t address, std::uint16_t count)
{
    std::vector<std::uint8_t> packet = makeCommand(kFlagAckRequired, kReadMemCmd, requestId, 8);
    writeBe32(&packet[kHeaderSize], address);
    writeBe16(&packet[kHeaderSize + 6], count);

    return packet;
}

std::vector<std::uint8_t> makeWriteMemCommand(std::uint16_t requestId, std::uint32_t address, const std::uint8_t* data,
                                              std::size_t size)
{
    std::vector<std::uint8_t> packet = makeCommand(kFlagAckRequired, kWriteMemCmd, requestId, 4 + size);
    writeBe32(&packet[kHeaderSize], address);
    for(std::size_t i = 0; i < size; ++i)
    {
        packet[kHeaderSize + 4 + i] = data[i];
    }

    return packet;
}

bool parseAckHeader(const std::uint8_t* data, std::size_t size, AckHeader* out)
{
    if(!data || !out || size < kHeaderSize)
    {
        return false;
    }
    out->status = readBe16(data);
    out->answer = readBe16(data + 2);
    out->length = readBe16(data + 4);
    out->ackId = readBe16(data + 6);

    return true;
}

const char* statusText(std::uint16_t status)
{
    switch(status)
    {
    case 0x0000: return "success";
    case 0x8001: return "not implemented";
    case 0x8002: return "invalid parameter";
    case 0x8003: return "invalid address";
    case 0x8004: return "write protected";
    case 0x8005: return "bad alignment";
    case 0x8006: return "access denied (another application controls the camera?)";
    case 0x8007: return "busy";
    case 0x800C: return "packet unavailable";
    case 0x800D: return "data overrun";
    case 0x800E: return "invalid header";
    case 0x800F: return "wrong configuration";
    case 0x8FFF: return "device error";
    default:     return "error";
    }
}

bool parseDiscoveryAck(const std::uint8_t* data, std::size_t size, DeviceInfo* out)
{
    if(!data || !out || size < kHeaderSize + kDiscoveryAckPayloadSize)
    {
        return false;
    }

    const std::uint16_t status = readBe16(data);
    const std::uint16_t answer = readBe16(data + 2);
    const std::uint16_t length = readBe16(data + 4);

    if(status != kStatusSuccess || answer != kDiscoveryAck || length < kDiscoveryAckPayloadSize)
    {
        return false;
    }

    const std::uint8_t* p = data + kHeaderSize;
    DeviceInfo info;
    info.versionMajor = readBe16(p + kVersion);
    info.versionMinor = readBe16(p + kVersion + 2);
    info.mac = {p[kMacHigh + 2], p[kMacHigh + 3], p[kMacLow], p[kMacLow + 1], p[kMacLow + 2], p[kMacLow + 3]};
    info.ip = readBe32(p + kCurrentIp);
    info.subnetMask = readBe32(p + kCurrentSubnetMask);
    info.gateway = readBe32(p + kCurrentGateway);
    info.manufacturer = readFixedString(p + kManufacturerName, 32);
    info.model = readFixedString(p + kModelName, 32);
    info.deviceVersion = readFixedString(p + kDeviceVersion, 32);
    info.manufacturerInfo = readFixedString(p + kManufacturerInfo, 48);
    info.serialNumber = readFixedString(p + kSerialNumber, 16);
    info.userDefinedName = readFixedString(p + kUserDefinedName, 16);

    *out = std::move(info);

    return true;
}

std::string macToString(const std::array<std::uint8_t, 6>& mac)
{
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                  mac[5]);

    return buffer;
}

std::string ipv4ToString(std::uint32_t address)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", (address >> 24) & 0xFFu, (address >> 16) & 0xFFu,
                  (address >> 8) & 0xFFu, address & 0xFFu);

    return buffer;
}

} // namespace calib::gvcp
