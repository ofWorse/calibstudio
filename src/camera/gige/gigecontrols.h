#ifndef CALIBSTUDIO_GIGECONTROLS_H
#define CALIBSTUDIO_GIGECONTROLS_H

#include "camera/cameracontrols.h"

#include <memory>

namespace calib
{

class GenICamNodeMap;

std::unique_ptr<ICameraControls> createGigEControls(GenICamNodeMap& nodes);

} // namespace calib

#endif // CALIBSTUDIO_GIGECONTROLS_H
