#ifndef CALIBSTUDIO_GVCPCLIENT_H
#define CALIBSTUDIO_GVCPCLIENT_H

#include "camera/gige/udpsocket.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace calib
{


// Control channel to one GigE Vision camera: register and memory access with
// request ids, retries and PENDING_ACK handling. Not thread-safe; owned by the
// thread that streams from the camera.
class GvcpClient
{
private:
    UdpSocket socket;
    std::uint32_t deviceIp = 0;
    std::uint16_t requestId = 0;
    int timeoutMs = 300;
    int retries = 3;
    std::vector<std::uint8_t> reply;

public:
    bool connect(std::uint32_t deviceIp, std::uint32_t hostIp, std::string* error);
    void disconnect(void);
    bool isConnected(void) const
    {
        return socket.isOpen();
    }

    bool readRegister(std::uint32_t address, std::uint32_t* value, std::string* error = nullptr);
    bool writeRegister(std::uint32_t address, std::uint32_t value, std::string* error = nullptr);
    bool readMemory(std::uint32_t address, void* data, std::size_t size, std::string* error = nullptr);
    bool writeMemory(std::uint32_t address, const void* data, std::size_t size, std::string* error = nullptr);

private:
    std::uint16_t nextRequestId(void);
    bool transact(const std::vector<std::uint8_t>& command, std::uint16_t expectedAck,
                  std::vector<std::uint8_t>* payload, std::string* error);
};


} // namespace calib

#endif // CALIBSTUDIO_GVCPCLIENT_H
