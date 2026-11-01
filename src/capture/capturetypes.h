#ifndef CALIBSTUDIO_CAPTURETYPES_H
#define CALIBSTUDIO_CAPTURETYPES_H

#include "camera/cameracontrols.h"
#include "camera/camerainfo.h"

#include <QMetaType>
#include <QVector>

Q_DECLARE_METATYPE(calib::CameraInfo)
Q_DECLARE_METATYPE(calib::CameraControlId)
Q_DECLARE_METATYPE(calib::CameraControlInfo)
Q_DECLARE_METATYPE(calib::CameraControlState)

namespace calib
{

void registerCaptureTypes(void);

} // namespace calib

#endif // CALIBSTUDIO_CAPTURETYPES_H
