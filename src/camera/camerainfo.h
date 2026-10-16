#ifndef CALIBSTUDIO_CAMERAINFO_H
#define CALIBSTUDIO_CAMERAINFO_H

#include <string>
#include <vector>

namespace calib
{

enum class CameraTransport
{
    Usb,
    GigE,
    Pci,
    Other
};


const char* toString(CameraTransport transport);


struct VideoMode
{
    int width = 0;
    int height = 0;
    double maxFps = 0.0;
    std::string fourcc;
    bool compressed = false;

    long long pixels(void) const
    {
        return static_cast<long long>(width) * height;
    }
};


struct CameraInfo
{
    std::string id;
    std::string name;
    std::string api;
    CameraTransport transport = CameraTransport::Other;
    std::string details;

    int captureApi = 0;
    int captureIndex = -1;

    bool streamable = false;
    std::string notStreamableReason;

    std::vector<VideoMode> modes;
};


std::string describe(const VideoMode& mode);
std::string describe(const CameraInfo& info);


} // namespace calib

#endif // CALIBSTUDIO_CAMERAINFO_H
