#include "camera/gige/udpsocket.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <cstring>

namespace calib
{

namespace
{

#ifdef _WIN32
using NativeSocket = SOCKET;

bool ensureWinsock(void)
{
    static const bool started = []()
    {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}

int lastError(void)
{
    return WSAGetLastError();
}

bool isTimeout(int error)
{
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
}

void closeNative(NativeSocket socket)
{
    closesocket(socket);
}
#else
using NativeSocket = int;

bool ensureWinsock(void)
{
    return true;
}

int lastError(void)
{
    return errno;
}

bool isTimeout(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

void closeNative(NativeSocket socket)
{
    ::close(socket);
}
#endif

NativeSocket native(std::intptr_t handle)
{
    return static_cast<NativeSocket>(handle);
}

sockaddr_in makeAddress(std::uint32_t ip, std::uint16_t port)
{
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ip);
    address.sin_port = htons(port);
    return address;
}

} // namespace

UdpSocket::~UdpSocket(void)
{
    close();
}

bool UdpSocket::open(std::uint32_t localIp, std::uint16_t localPort, std::string* error)
{
    close();
    if(!ensureWinsock())
    {
        if(error)
        {
            *error = "Winsock initialisation failed";
        }
        return false;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if(socket == INVALID_SOCKET)
#else
    if(socket < 0)
#endif
    {
        if(error)
        {
            *error = "cannot create a UDP socket (error " + std::to_string(lastError()) + ")";
        }
        return false;
    }

    const sockaddr_in address = makeAddress(localIp, localPort);
    if(::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        if(error)
        {
            *error = "cannot bind a UDP socket (error " + std::to_string(lastError()) + ")";
        }
        closeNative(socket);
        return false;
    }

    handle = static_cast<std::intptr_t>(socket);
    receiveTimeoutMs = -1;
    return true;
}

void UdpSocket::close(void)
{
    if(handle != -1)
    {
        closeNative(native(handle));
        handle = -1;
    }
}

std::uint16_t UdpSocket::getLocalPort(void) const
{
    if(handle == -1)
    {
        return 0;
    }
    sockaddr_in address;
    socklen_t length = sizeof(address);
    if(::getsockname(native(handle), reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        return 0;
    }
    return ntohs(address.sin_port);
}

bool UdpSocket::setBroadcast(bool enabled)
{
    const int value = enabled ? 1 : 0;
    return ::setsockopt(native(handle), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&value), sizeof(value))
        == 0;
}

int UdpSocket::setReceiveBufferSize(int bytes)
{
    ::setsockopt(native(handle), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
    int granted = 0;
    socklen_t length = sizeof(granted);
    ::getsockopt(native(handle), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&granted), &length);
    return granted;
}

bool UdpSocket::sendTo(const void* data, std::size_t size, std::uint32_t ip, std::uint16_t port)
{
    if(handle == -1)
    {
        return false;
    }
    const sockaddr_in address = makeAddress(ip, port);
    const auto sent = ::sendto(native(handle), static_cast<const char*>(data), static_cast<int>(size), 0,
                               reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    return sent >= 0 && static_cast<std::size_t>(sent) == size;
}

int UdpSocket::receive(void* buffer, std::size_t capacity, int timeoutMs, std::uint32_t* fromIp,
                       std::uint16_t* fromPort)
{
    if(handle == -1)
    {
        return -1;
    }

    const int wanted = timeoutMs < 1 ? 1 : timeoutMs;
    if(wanted != receiveTimeoutMs)
    {
#ifdef _WIN32
        const DWORD value = static_cast<DWORD>(wanted);
#else
        timeval value;
        value.tv_sec = wanted / 1000;
        value.tv_usec = (wanted % 1000) * 1000;
#endif
        ::setsockopt(native(handle), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value));
        receiveTimeoutMs = wanted;
    }

    sockaddr_in address;
    socklen_t length = sizeof(address);
    const auto received = ::recvfrom(native(handle), static_cast<char*>(buffer), static_cast<int>(capacity), 0,
                                     reinterpret_cast<sockaddr*>(&address), &length);
    if(received < 0)
    {
        const int error = lastError();
#ifdef _WIN32
        if(error == WSAEMSGSIZE)
        {
            return static_cast<int>(capacity);
        }
        if(error == WSAECONNRESET)
        {
            return 0;
        }
#endif
        return isTimeout(error) ? 0 : -1;
    }

    if(fromIp)
    {
        *fromIp = ntohl(address.sin_addr.s_addr);
    }
    if(fromPort)
    {
        *fromPort = ntohs(address.sin_port);
    }
    return static_cast<int>(received);
}


} // namespace calib
