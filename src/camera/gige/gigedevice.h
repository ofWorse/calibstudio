#ifndef CALIBSTUDIO_GIGEDEVICE_H
#define CALIBSTUDIO_GIGEDEVICE_H

#include "camera/camerainfo.h"
#include "camera/gige/genicam.h"
#include "camera/gige/gvcpclient.h"
#include "camera/gige/gvsp.h"
#include "camera/gige/udpsocket.h"

#include <opencv2/core/mat.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace calib
{


// Streams from one GigE Vision camera without a vendor SDK:
//   GVCP  take control, read the GenICam XML, point the stream channel at us
//   GenICam  find and run AcquisitionStart / AcquisitionStop
//   GVSP  receive and reassemble the image packets
// Owned and used by one thread.
class GigEDevice
{
private:
    class ControlPort;

    GvcpClient control;
    UdpSocket stream;
    GenICamNodeMap nodes;
    std::unique_ptr<ControlPort> port;
    gvsp::FrameAssembler assembler;
    std::vector<std::uint8_t> packet;
    std::uint32_t deviceIp = 0;
    bool connected = false;
    bool opened = false;
    bool acquiring = false;
    std::uint32_t packetSize = 0;
    std::uint16_t streamSourcePort = 0;
    std::chrono::steady_clock::time_point nextHeartbeat;
    int failedHeartbeats = 0;
    std::string lastError;

public:
    GigEDevice(void);
    ~GigEDevice(void);
    GigEDevice(const GigEDevice&) = delete;
    GigEDevice& operator=(const GigEDevice&) = delete;

    bool open(const CameraInfo& camera, std::string* error);
    void close(void);
    bool isOpened(void) const
    {
        return opened;
    }

    bool read(cv::Mat& frame, int timeoutMs);

    const std::string& getLastError(void) const
    {
        return lastError;
    }
    std::uint32_t getPacketSize(void) const
    {
        return packetSize;
    }
    std::uint32_t getPixelFormat(void) const
    {
        return assembler.getPixelFormat();
    }
    std::uint64_t getIncompleteFrames(void) const
    {
        return assembler.getIncompleteFrames();
    }
    std::uint64_t getLostPackets(void) const
    {
        return assembler.getLostPackets();
    }
    std::uint64_t getReceivedPackets(void) const
    {
        return assembler.getPackets();
    }
    GenICamNodeMap& getNodeMap(void)
    {
        return nodes;
    }

private:
    bool loadNodeMap(std::string* error);
    bool configureStream(std::uint32_t hostIp, std::string* error);
    void prepareAcquisition(void);
    bool keepAlive(void);
    void openFirewallPath(void);
};


} // namespace calib

#endif // CALIBSTUDIO_GIGEDEVICE_H
