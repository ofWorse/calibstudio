#include "gvcp.h"

#include <cstdio>
#include <utility>

namespace calib::gvcp
{

namespace
{

constexpr std::size_t kVersion = 0x00;          // major (2) + minor (2)
constexpr std::size_t kMacHigh = 0x08;          // 2 reserved bytes + MAC[0..1]
constexpr std::size_t kMacLow = 0x0C;           // MAC[2..5]
constexpr std::size_t kCurrentIp = 0x24;
constexpr std::size_t kCurrentSubnetMask = 0x34;
constexpr std::size_t kCurrentGateway = 0x44;
constexpr std::size_t kManufacturerName = 0x48; // 32 bytes
constexpr std::size_t kModelName = 0x68;        // 32 bytes
constexpr std::size_t kDeviceVersion = 0x88;    // 32 bytes
constexpr std::size_t kManufacturerInfo = 0xA8; // 48 bytes
constexpr std::size_t kSerialNumber = 0xD8;     // 16 bytes
constexpr std::size_t kUserDefinedName = 0xE8;  // 16 bytes

std::uint16_t readBe16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::uint32_t readBe32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

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

} // namespace

std::vector<std::uint8_t> makeDiscoveryCommand(std::uint16_t requestId, bool allowBroadcastAck)
{
    std::vector<std::uint8_t> packet(kHeaderSize, 0);
    packet[0] = kKey;
    packet[1] = static_cast<std::uint8_t>(kFlagAckRequired
                                          | (allowBroadcastAck ? kFlagAllowBroadcastAck : 0));
    packet[2] = static_cast<std::uint8_t>(kDiscoveryCmd >> 8);
    packet[3] = static_cast<std::uint8_t>(kDiscoveryCmd & 0xFF);
    packet[6] = static_cast<std::uint8_t>(requestId >> 8);
    packet[7] = static_cast<std::uint8_t>(requestId & 0xFF);

    return packet;
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
    info.mac = { p[kMacHigh + 2], p[kMacHigh + 3],
                 p[kMacLow],      p[kMacLow + 1], p[kMacLow + 2], p[kMacLow + 3] };
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
    std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return buffer;
}

std::string ipv4ToString(std::uint32_t address)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
                  (address >> 24) & 0xFFu, (address >> 16) & 0xFFu,
                  (address >> 8) & 0xFFu, address & 0xFFu);

    return buffer;
}

} // namespace calib::gvcp
