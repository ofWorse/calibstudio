#ifndef CALIBSTUDIO_UDPSOCKET_H
#define CALIBSTUDIO_UDPSOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace calib
{


// Minimal blocking IPv4 UDP socket, Winsock or BSD sockets.
class UdpSocket
{
private:
    std::intptr_t handle = -1;
    int receiveTimeoutMs = -1;

public:
    UdpSocket(void) = default;
    ~UdpSocket(void);
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(std::uint32_t localIp, std::uint16_t localPort, std::string* error);
    void close(void);
    bool isOpen(void) const
    {
        return handle != -1;
    }

    std::uint16_t getLocalPort(void) const;
    bool setBroadcast(bool enabled);
    int setReceiveBufferSize(int bytes);

    bool sendTo(const void* data, std::size_t size, std::uint32_t ip, std::uint16_t port);
    int receive(void* buffer, std::size_t capacity, int timeoutMs, std::uint32_t* fromIp = nullptr,
                std::uint16_t* fromPort = nullptr);
};


} // namespace calib

#endif // CALIBSTUDIO_UDPSOCKET_H
