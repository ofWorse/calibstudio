#include "camera/cameraprofile.h"

namespace calib
{


std::string profileKey(const CameraInfo& camera)
{
    if(camera.transport == CameraTransport::Usb && !camera.details.empty())
    {
        return camera.name + " | " + camera.details;
    }
    return camera.id;
}

int findMode(const std::vector<VideoMode>& modes, const VideoMode& wanted)
{
    int sameSize = -1;
    for(size_t i = 0; i < modes.size(); ++i)
    {
        const VideoMode& mode = modes[i];
        if(mode.width != wanted.width || mode.height != wanted.height)
        {
            continue;
        }
        if(mode.fourcc == wanted.fourcc)
        {
            return static_cast<int>(i);
        }
        if(sameSize < 0)
        {
            sameSize = static_cast<int>(i);
        }
    }
    return sameSize;
}


} // namespace calib
