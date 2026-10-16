#include "camera/directshow/directshowcontrols.h"

#ifdef _WIN32

#include "camera/directshow/dshowcommon.h"

#include <algorithm>
#include <utility>

namespace calib
{

namespace
{


using dshow::ComPtr;
using dshow::readStringProperty;

struct ControlMapping
{
    CameraControlId id;
    bool lensControl;
    long property;
};

constexpr ControlMapping kMappings[] =
{
    {CameraControlId::Focus,                 true,  CameraControl_Focus               },
    {CameraControlId::Exposure,              true,  CameraControl_Exposure            },
    {CameraControlId::Gain,                  false, VideoProcAmp_Gain                 },
    {CameraControlId::Brightness,            false, VideoProcAmp_Brightness           },
    {CameraControlId::Contrast,              false, VideoProcAmp_Contrast             },
    {CameraControlId::Saturation,            false, VideoProcAmp_Saturation           },
    {CameraControlId::Hue,                   false, VideoProcAmp_Hue                  },
    {CameraControlId::Gamma,                 false, VideoProcAmp_Gamma                },
    {CameraControlId::Sharpness,             false, VideoProcAmp_Sharpness            },
    {CameraControlId::WhiteBalance,          false, VideoProcAmp_WhiteBalance         },
    {CameraControlId::BacklightCompensation, false, VideoProcAmp_BacklightCompensation},
};


constexpr long kFlagAuto = 0x0001;
constexpr long kFlagManual = 0x0002;


const ControlMapping* findMapping(CameraControlId id)
{
    for(const ControlMapping& mapping : kMappings)
    {
        if(mapping.id == id)
        {
            return &mapping;
        }
    }
    return nullptr;
}


bool bindCameraFilter(const CameraInfo& camera, IBaseFilter** filter)
{
    ComPtr<ICreateDevEnum> deviceEnum;
    if(FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum,
                               deviceEnum.putVoid())))
    {
        return false;
    }

    ComPtr<IEnumMoniker> monikers;
    if(deviceEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, monikers.put(), 0) != S_OK || !monikers)
    {
        return false;
    }

    const std::string wantedPath = camera.id.rfind("dshow:", 0) == 0 ? std::string() : camera.id;

    ComPtr<IMoniker> moniker;
    ComPtr<IMoniker> fallback;
    int index = 0;
    while(monikers->Next(1, moniker.put(), nullptr) == S_OK)
    {
        std::string friendlyName;
        std::string devicePath;
        ComPtr<IPropertyBag> properties;
        if(SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, properties.putVoid())))
        {
            friendlyName = readStringProperty(properties.get(), L"FriendlyName");
            devicePath = readStringProperty(properties.get(), L"DevicePath");
        }

        const bool samePath = !wantedPath.empty() && devicePath == wantedPath;
        const bool sameName = friendlyName == camera.name;
        if(index == camera.captureIndex && (samePath || sameName))
        {
            return SUCCEEDED(
                moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(filter)));
        }
        if(!fallback && (samePath || (wantedPath.empty() && sameName)))
        {
            moniker->AddRef();
            *fallback.put() = moniker.get();
        }
        ++index;
    }

    return fallback
        && SUCCEEDED(fallback->BindToObject(nullptr, nullptr, IID_IBaseFilter, reinterpret_cast<void**>(filter)));
}


class DirectShowCameraControls final : public ICameraControls
{
private:
    ComPtr<IBaseFilter> filter;
    ComPtr<IAMVideoProcAmp> procAmp;
    ComPtr<IAMCameraControl> lensControl;
    std::vector<CameraControlInfo> supported;

public:
    bool initialise(const CameraInfo& camera)
    {
        if(!bindCameraFilter(camera, filter.put()))
        {
            return false;
        }
        filter->QueryInterface(IID_IAMVideoProcAmp, procAmp.putVoid());
        filter->QueryInterface(IID_IAMCameraControl, lensControl.putVoid());

        for(const ControlMapping& mapping : kMappings)
        {
            long minimum = 0;
            long maximum = 0;
            long step = 0;
            long defaultValue = 0;
            long flags = 0;
            HRESULT hr = E_NOINTERFACE;
            if(mapping.lensControl && lensControl)
            {
                hr = lensControl->GetRange(mapping.property, &minimum, &maximum, &step, &defaultValue, &flags);
            }
            else if(!mapping.lensControl && procAmp)
            {
                hr = procAmp->GetRange(mapping.property, &minimum, &maximum, &step, &defaultValue, &flags);
            }
            if(FAILED(hr) || maximum <= minimum)
            {
                continue;
            }

            CameraControlInfo control;
            control.id = mapping.id;
            control.minimum = static_cast<int>(minimum);
            control.maximum = static_cast<int>(maximum);
            control.step = std::max(1, static_cast<int>(step));
            control.defaultValue = static_cast<int>(std::clamp(defaultValue, minimum, maximum));
            control.supportsAuto = (flags & kFlagAuto) != 0;
            control.supportsManual = (flags & kFlagManual) != 0 || flags == 0;
            supported.push_back(control);
        }
        return !supported.empty();
    }

    std::vector<CameraControlInfo> list(void) const override
    {
        return supported;
    }

    bool get(CameraControlId id, CameraControlState* state) const override
    {
        const ControlMapping* mapping = findMapping(id);
        if(!mapping || !state)
        {
            return false;
        }

        long value = 0;
        long flags = 0;
        HRESULT hr = E_NOINTERFACE;
        if(mapping->lensControl && lensControl)
        {
            hr = lensControl->Get(mapping->property, &value, &flags);
        }
        else if(!mapping->lensControl && procAmp)
        {
            hr = procAmp->Get(mapping->property, &value, &flags);
        }
        if(FAILED(hr))
        {
            return false;
        }

        state->value = static_cast<int>(value);
        state->automatic = (flags & kFlagAuto) != 0;
        return true;
    }

    bool set(CameraControlId id, const CameraControlState& state) override
    {
        const ControlMapping* mapping = findMapping(id);
        const auto control = std::find_if(supported.begin(), supported.end(),
                                          [id](const CameraControlInfo& c) { return c.id == id; });
        if(!mapping || control == supported.end())
        {
            return false;
        }

        long value = std::clamp(state.value, control->minimum, control->maximum);
        value = control->minimum + (value - control->minimum + control->step / 2) / control->step * control->step;
        value = std::min<long>(value, control->maximum);

        const bool automatic = state.automatic && control->supportsAuto;
        const long flags = automatic ? kFlagAuto : kFlagManual;

        HRESULT hr = E_NOINTERFACE;
        if(mapping->lensControl && lensControl)
        {
            hr = lensControl->Set(mapping->property, value, flags);
        }
        else if(!mapping->lensControl && procAmp)
        {
            hr = procAmp->Set(mapping->property, value, flags);
        }
        return SUCCEEDED(hr);
    }
};


} // namespace

std::unique_ptr<ICameraControls> createDirectShowControls(const CameraInfo& camera)
{
    auto controls = std::make_unique<DirectShowCameraControls>();
    if(!controls->initialise(camera))
    {
        return nullptr;
    }
    return controls;
}


} // namespace calib

#endif // _WIN32
