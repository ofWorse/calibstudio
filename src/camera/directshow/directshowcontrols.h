#ifndef CALIBSTUDIO_DIRECTSHOWCONTROLS_H
#define CALIBSTUDIO_DIRECTSHOWCONTROLS_H

#include "camera/cameracontrols.h"

namespace calib
{


std::unique_ptr<ICameraControls> createDirectShowControls(const CameraInfo& camera);


} // namespace calib

#endif // CALIBSTUDIO_DIRECTSHOWCONTROLS_H
