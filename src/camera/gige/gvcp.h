#ifndef CALIBSTUDIO_GVCP_H
#define CALIBSTUDIO_GVCP_H


#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// GigE Vision Control Protocol (GVCP): message layouts and bootstrap registers
// from the GigE Vision specification. All fields are big-endian.
namespace calib::gvcp
{

constexpr std::uint16_t kPort = 3956;
constexpr std::uint8_t kKey = 0x42;
constexpr std::uint8_t kFlagAckRequired = 0x01;
constexpr std::uint8_t kFlagAllowBroadcastAck = 0x10;

constexpr std::uint16_t kDiscoveryCmd = 0x0002;
constexpr std::uint16_t kDiscoveryAck = 0x0003;
constexpr std::uint16_t kForceIpCmd = 0x0004;
constexpr std::uint16_t kForceIpAck = 0x0005;
constexpr std::uint16_t kReadRegCmd = 0x0080;
constexpr std::uint16_t kReadRegAck = 0x0081;
constexpr std::uint16_t kWriteRegCmd = 0x0082;
constexpr std::uint16_t kWriteRegAck = 0x0083;
constexpr std::uint16_t kReadMemCmd = 0x0084;
constexpr std::uint16_t kReadMemAck = 0x0085;
constexpr std::uint16_t kWriteMemCmd = 0x0086;
constexpr std::uint16_t kWriteMemAck = 0x0087;
constexpr std::uint16_t kPendingAck = 0x0089;

constexpr std::uint16_t kStatusSuccess = 0x0000;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kDiscoveryAckPayloadSize = 0xF8;
constexpr std::size_t kForceIpPayloadSize = 0x38;
constexpr std::size_t kMaxMemoryChunk = 512;

// Bootstrap registers every GigE Vision device implements at fixed addresses.
namespace reg
{

constexpr std::uint32_t kFirstUrl = 0x0200;
constexpr std::uint32_t kUrlSize = 512;
constexpr std::uint32_t kHeartbeatTimeout = 0x0938;
constexpr std::uint32_t kControlChannelPrivilege = 0x0A00;
constexpr std::uint32_t kStreamChannelPort0 = 0x0D00;
constexpr std::uint32_t kStreamChannelPacketSize0 = 0x0D04;
constexpr std::uint32_t kStreamChannelDestination0 = 0x0D18;
constexpr std::uint32_t kStreamChannelSourcePort0 = 0x0D1C;

} // namespace reg

constexpr std::uint32_t kPrivilegeExclusive = 0x1;
constexpr std::uint32_t kPrivilegeControl = 0x2;
constexpr std::uint32_t kPacketSizeMask = 0xFFFF;

struct DeviceInfo
{
    std::uint16_t versionMajor = 0;
    std::uint16_t versionMinor = 0;
    std::array<std::uint8_t, 6> mac{};
    std::uint32_t ip = 0;
    std::uint32_t subnetMask = 0;
    std::uint32_t gateway = 0;
    std::string manufacturer;
    std::string model;
    std::string deviceVersion;
    std::string manufacturerInfo;
    std::string serialNumber;
    std::string userDefinedName;
};

struct AckHeader
{
    std::uint16_t status = 0;
    std::uint16_t answer = 0;
    std::uint16_t length = 0;
    std::uint16_t ackId = 0;
};


inline std::uint16_t readBe16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

inline std::uint32_t readBe32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

inline void writeBe16(std::uint8_t* p, std::uint16_t value)
{
    p[0] = static_cast<std::uint8_t>(value >> 8);
    p[1] = static_cast<std::uint8_t>(value & 0xFF);
}

inline void writeBe32(std::uint8_t* p, std::uint32_t value)
{
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    p[2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(value & 0xFF);
}


std::vector<std::uint8_t> makeDiscoveryCommand(std::uint16_t requestId, bool allowBroadcastAck);
bool parseDiscoveryAck(const std::uint8_t* data, std::size_t size, DeviceInfo* out);


std::vector<std::uint8_t> makeForceIpCommand(std::uint16_t requestId, const std::array<std::uint8_t, 6>& mac,
                                             std::uint32_t ip, std::uint32_t subnetMask, std::uint32_t gateway);

std::vector<std::uint8_t> makeReadRegCommand(std::uint16_t requestId, const std::vector<std::uint32_t>& addresses);
std::vector<std::uint8_t> makeWriteRegCommand(std::uint16_t requestId,
                                              const std::vector<std::pair<std::uint32_t, std::uint32_t>>& writes);
std::vector<std::uint8_t> makeReadMemCommand(std::uint16_t requestId, std::uint32_t address, std::uint16_t count);
std::vector<std::uint8_t> makeWriteMemCommand(std::uint16_t requestId, std::uint32_t address, const std::uint8_t* data,
                                              std::size_t size);

bool parseAckHeader(const std::uint8_t* data, std::size_t size, AckHeader* out);
const char* statusText(std::uint16_t status);

std::string macToString(const std::array<std::uint8_t, 6>& mac);
std::string ipv4ToString(std::uint32_t address);


} // namespace calib::gvcp

#endif // CALIBSTUDIO_GVCP_H
