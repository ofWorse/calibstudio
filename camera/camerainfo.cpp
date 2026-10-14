#include "camerainfo.h"

#include <cstdio>

namespace calib
{


const char* toString(CameraTransport transport)
{
    switch (transport)
    {
    case CameraTransport::Usb:   return "USB";
    case CameraTransport::GigE:  return "GigE";
    case CameraTransport::Pci:   return "PCIe";
    case CameraTransport::Other: return "Other";
    }
    return "Other";
}

std::string describe(const VideoMode& mode)
{
    char buffer[96];
    if(mode.maxFps > 0.0)
    {
        std::snprintf(buffer, sizeof(buffer), "%dx%d %s @ %.0f fps",
                      mode.width, mode.height, mode.fourcc.c_str(), mode.maxFps);
    } else
    {
        std::snprintf(buffer, sizeof(buffer), "%dx%d %s", mode.width, mode.height, mode.fourcc.c_str());
    }
    return buffer;
}

std::string describe(const CameraInfo& info)
{
    std::string text = "[";
    text += toString(info.transport);
    text += "] ";
    text += info.name;

    if(!info.details.empty())
    {
        text += " (" + info.details + ")";
    }

    return text;
}


} // namespace calib
