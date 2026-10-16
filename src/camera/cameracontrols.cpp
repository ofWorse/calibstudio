#include "camera/cameracontrols.h"

#ifdef _WIN32
#include "camera/directshow/directshowcontrols.h"
#endif

#include <cstdio>

namespace calib
{


const char* toString(CameraControlId id)
{
    switch(id)
    {
    case CameraControlId::Brightness:            return "Brightness";
    case CameraControlId::Contrast:              return "Contrast";
    case CameraControlId::Saturation:            return "Saturation";
    case CameraControlId::Hue:                   return "Hue";
    case CameraControlId::Gamma:                 return "Gamma";
    case CameraControlId::Sharpness:             return "Sharpness";
    case CameraControlId::WhiteBalance:          return "White balance";
    case CameraControlId::BacklightCompensation: return "Backlight compensation";
    case CameraControlId::Gain:                  return "Gain";
    case CameraControlId::Exposure:              return "Exposure";
    case CameraControlId::Focus:                 return "Focus";
    }
    return "Unknown";
}

const char* toKey(CameraControlId id)
{
    switch(id)
    {
    case CameraControlId::Brightness:            return "brightness";
    case CameraControlId::Contrast:              return "contrast";
    case CameraControlId::Saturation:            return "saturation";
    case CameraControlId::Hue:                   return "hue";
    case CameraControlId::Gamma:                 return "gamma";
    case CameraControlId::Sharpness:             return "sharpness";
    case CameraControlId::WhiteBalance:          return "white_balance";
    case CameraControlId::BacklightCompensation: return "backlight_compensation";
    case CameraControlId::Gain:                  return "gain";
    case CameraControlId::Exposure:              return "exposure";
    case CameraControlId::Focus:                 return "focus";
    }
    return "unknown";
}

bool controlIdFromKey(const std::string& key, CameraControlId* id)
{
    for(int i = static_cast<int>(CameraControlId::Brightness); i <= static_cast<int>(CameraControlId::Focus); ++i)
    {
        const auto candidate = static_cast<CameraControlId>(i);
        if(key == toKey(candidate))
        {
            *id = candidate;
            return true;
        }
    }
    return false;
}

std::string formatControlValue(CameraControlId id, int value)
{
    char buffer[32];
    if(id == CameraControlId::Exposure)
    {
        if(value < 0 && value > -31)
        {
            std::snprintf(buffer, sizeof(buffer), "1/%ld s", 1L << (-value));
        }
        else if(value >= 0 && value < 31)
        {
            std::snprintf(buffer, sizeof(buffer), "%ld s", 1L << value);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%d", value);
        }
        return buffer;
    }
    if(id == CameraControlId::WhiteBalance)
    {
        std::snprintf(buffer, sizeof(buffer), "%d K", value);
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

std::unique_ptr<ICameraControls> createCameraControls(const CameraInfo& camera)
{
#ifdef _WIN32
    if(camera.api == "DirectShow")
    {
        return createDirectShowControls(camera);
    }
#else
    (void)camera;
#endif
    return nullptr;
}


} // namespace calib
