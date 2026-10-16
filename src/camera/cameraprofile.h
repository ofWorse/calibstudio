#ifndef CALIBSTUDIO_CAMERAPROFILE_H
#define CALIBSTUDIO_CAMERAPROFILE_H

#include "camera/cameracontrols.h"
#include "camera/camerainfo.h"

#include <map>
#include <string>

namespace calib
{


struct CameraProfile
{
    bool hasMode = false;
    VideoMode mode;
    std::map<CameraControlId, CameraControlState> controls;
    bool flipHorizontal = false;
    bool flipVertical = false;
};


std::string profileKey(const CameraInfo& camera);
int findMode(const std::vector<VideoMode>& modes, const VideoMode& wanted);


} // namespace calib

#endif // CALIBSTUDIO_CAMERAPROFILE_H
