#ifndef CALIBSTUDIO_CAMERACONTROLS_H
#define CALIBSTUDIO_CAMERACONTROLS_H

#include "camera/camerainfo.h"

#include <memory>
#include <string>
#include <vector>

namespace calib
{


enum class CameraControlId
{
    Brightness,
    Contrast,
    Saturation,
    Hue,
    Gamma,
    Sharpness,
    WhiteBalance,
    BacklightCompensation,
    Gain,
    Exposure,
    Focus,
};

enum class ControlMode
{
    Manual,
    Once,
    Continuous,
    Auto,
};

enum class ControlUnit
{
    Raw,
    Log2Seconds,
    Microseconds,
    TenthDecibel,
};

const char* toString(CameraControlId id);
const char* toKey(CameraControlId id);
bool controlIdFromKey(const std::string& key, CameraControlId* id);
const char* toString(ControlMode mode);
const char* toKey(ControlMode mode);
bool controlModeFromKey(const std::string& key, ControlMode* mode);
std::string formatControlValue(CameraControlId id, int value);
std::string formatControlValue(CameraControlId id, ControlUnit unit, int value);


struct CameraControlInfo
{
    CameraControlId id = CameraControlId::Brightness;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int defaultValue = 0;
    bool supportsAuto = false;
    bool supportsManual = true;
    ControlUnit unit = ControlUnit::Raw;
};

struct CameraControlState
{
    int value = 0;
    bool automatic = false;
    ControlMode mode = ControlMode::Manual;
};

// Exposure and gain can also be adjusted by CalibStudio itself (Once / Continuous).
bool supportsSoftwareAuto(const CameraControlInfo& control);


class ICameraControls
{
public:
    virtual ~ICameraControls(void) = default;

    virtual std::vector<CameraControlInfo> list(void) const = 0;
    virtual bool get(CameraControlId id, CameraControlState* state) const = 0;
    virtual bool set(CameraControlId id, const CameraControlState& state) = 0;
};

std::unique_ptr<ICameraControls> createCameraControls(const CameraInfo& camera);


} // namespace calib

#endif // CALIBSTUDIO_CAMERACONTROLS_H
