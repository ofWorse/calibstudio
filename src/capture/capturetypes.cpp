#include "capture/capturetypes.h"

namespace calib
{

void registerCaptureTypes(void)
{
    qRegisterMetaType<calib::CameraInfo>("calib::CameraInfo");
    qRegisterMetaType<QVector<calib::CameraInfo>>("QVector<calib::CameraInfo>");
    qRegisterMetaType<calib::CameraControlId>("calib::CameraControlId");
    qRegisterMetaType<calib::CameraControlState>("calib::CameraControlState");
    qRegisterMetaType<QVector<calib::CameraControlInfo>>("QVector<calib::CameraControlInfo>");
    qRegisterMetaType<QVector<calib::CameraControlState>>("QVector<calib::CameraControlState>");
}

} // namespace calib
