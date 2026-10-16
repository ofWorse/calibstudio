#ifndef CALIBSTUDIO_GVCP_H
#define CALIBSTUDIO_GVCP_H


#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace calib::gvcp
{

constexpr std::uint16_t kPort = 3956;                 // UDP port of the control channel
constexpr std::uint8_t kKey = 0x42;                   // first byte of every command
constexpr std::uint8_t kFlagAckRequired = 0x01;
constexpr std::uint8_t kFlagAllowBroadcastAck = 0x10; // discovery only (GigE Vision 1.1+)
constexpr std::uint16_t kDiscoveryCmd = 0x0002;
constexpr std::uint16_t kDiscoveryAck = 0x0003;
constexpr std::uint16_t kStatusSuccess = 0x0000;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kDiscoveryAckPayloadSize = 0xF8;

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


std::vector<std::uint8_t> makeDiscoveryCommand(std::uint16_t requestId, bool allowBroadcastAck);
bool parseDiscoveryAck(const std::uint8_t* data, std::size_t size, DeviceInfo* out);

std::string macToString(const std::array<std::uint8_t, 6>& mac);
std::string ipv4ToString(std::uint32_t address);


} // namespace calib::gvcp

#endif // CALIBSTUDIO_GVCP_H
