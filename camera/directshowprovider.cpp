#include "directshowprovider.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>

#include <opencv2/videoio.hpp>
#include <opencv2/videoio/registry.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace calib
{

namespace
{


template <class T>
class ComPtr
{
private:
    T* ptr = nullptr;

public:
    ComPtr(void) = default;
    ~ComPtr(void)
    { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T* get(void) const
    { return ptr; }

    T* operator->(void) const
    { return ptr; }

    explicit operator bool(void) const
    { return ptr != nullptr; }

    T** put(void)
    { reset(); return &ptr; }

    void** putVoid(void)
    { return reinterpret_cast<void**>(put()); }

    void reset(void)
    {
        if(ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }
};


class ComScope
{
private:
    bool owned = false;
    bool usable = false;

public:
    ComScope(void)
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owned = SUCCEEDED(hr);
        usable = owned || hr == RPC_E_CHANGED_MODE;
    }

    ~ComScope(void)
    {
        if(owned)
        {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    bool getUsable(void) const { return usable; }
};


std::string hresultText(const char* what, HRESULT hr)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08lX)",
                  what, static_cast<unsigned long>(hr));
    return buffer;
}

std::string toUtf8(const wchar_t* text)
{
    if(!text || !*text)
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if(size <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));

    return result;
}

std::string readStringProperty(IPropertyBag* bag, const wchar_t* key)
{
    VARIANT value;
    VariantInit(&value);
    std::string result;
    if(SUCCEEDED(bag->Read(key, &value, nullptr)) && value.vt == VT_BSTR)
    {
        result = toUtf8(value.bstrVal);
    }

    VariantClear(&value);

    return result;
}

void freeMediaType(AM_MEDIA_TYPE* type)
{
    if(!type)
    {
        return;
    }
    if(type->cbFormat != 0 && type->pbFormat)
    {
        CoTaskMemFree(type->pbFormat);
    }
    if(type->pUnk)
    {
        type->pUnk->Release();
    }

    CoTaskMemFree(type);
}

std::string subtypeToFourcc(const GUID& subtype)
{
    if(subtype == MEDIASUBTYPE_RGB24)
    {
        return "RGB24";
    }
    if(subtype == MEDIASUBTYPE_RGB32)
    {
        return "RGB32";
    }

    std::string code(4, '?');
    for(int i = 0; i < 4; ++i)
    {
        const auto c = static_cast<unsigned char>((subtype.Data1 >> (8 * i)) & 0xFFu);
        if(std::isprint(c))
        {
            code[static_cast<size_t>(i)] = static_cast<char>(c);
        }
    }

    return code;
}

bool isCompressedFourcc(const std::string& fourcc)
{
    return fourcc == "MJPG" || fourcc == "H264" || fourcc == "H265" || fourcc == "HEVC";
}

bool isCapturePin(IPin* pin)
{
    ComPtr<IKsPropertySet> properties;
    if(FAILED(pin->QueryInterface(IID_IKsPropertySet, properties.putVoid())))
    {
        return true;
    }

    GUID category{};
    DWORD returned = 0;
    if(FAILED(properties->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0,
                               &category, sizeof(category), &returned)))
    {
        return true;
    }

    return category == PIN_CATEGORY_CAPTURE;
}

void addMode(std::vector<VideoMode>& modes, VideoMode mode)
{
    for(VideoMode& existing : modes)
    {
        if(existing.width == mode.width && existing.height == mode.height
            && existing.fourcc == mode.fourcc)
        {
            existing.maxFps = std::max(existing.maxFps, mode.maxFps);
            return;
        }
    }

    modes.push_back(std::move(mode));
}

std::vector<VideoMode> queryModes(IBaseFilter* filter)
{
    std::vector<VideoMode> modes;

    ComPtr<IEnumPins> pins;
    if(FAILED(filter->EnumPins(pins.put())))
    {
        return modes;
    }

    ComPtr<IAMStreamConfig> config;
    ComPtr<IPin> pin;
    while(!config && pins->Next(1, pin.put(), nullptr) == S_OK)
    {
        PIN_DIRECTION direction = PINDIR_INPUT;
        if(FAILED(pin->QueryDirection(&direction)) || direction != PINDIR_OUTPUT)
        {
            continue;
        }
        if(!isCapturePin(pin.get()))
        {
            continue;
        }

        pin->QueryInterface(IID_IAMStreamConfig, config.putVoid());
    }
    if(!config)
    {
        return modes;
    }

    int count = 0;
    int capsSize = 0;
    if(FAILED(config->GetNumberOfCapabilities(&count, &capsSize))
        || capsSize != static_cast<int>(sizeof(VIDEO_STREAM_CONFIG_CAPS)))
    {
        return modes;
    }

    for(int i = 0; i < count; ++i)
    {
        AM_MEDIA_TYPE* type = nullptr;
        VIDEO_STREAM_CONFIG_CAPS caps{};
        if(FAILED(config->GetStreamCaps(i, &type, reinterpret_cast<BYTE*>(&caps))) || !type)
        {
            continue;
        }

        const BITMAPINFOHEADER* header = nullptr;
        REFERENCE_TIME defaultInterval = 0;
        if(type->majortype == MEDIATYPE_Video && type->pbFormat)
        {
            if(type->formattype == FORMAT_VideoInfo
                && type->cbFormat >= sizeof(VIDEOINFOHEADER))
            {
                const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(type->pbFormat);
                header = &info->bmiHeader;
                defaultInterval = info->AvgTimePerFrame;
            } else if(type->formattype == FORMAT_VideoInfo2
                       && type->cbFormat >= sizeof(VIDEOINFOHEADER2))
            {
                const auto* info = reinterpret_cast<const VIDEOINFOHEADER2*>(type->pbFormat);
                header = &info->bmiHeader;
                defaultInterval = info->AvgTimePerFrame;
            }
        }

        if(header && header->biWidth > 0 && header->biHeight != 0)
        {
            VideoMode mode;
            mode.width = static_cast<int>(header->biWidth);
            mode.height = static_cast<int>(std::abs(header->biHeight));
            const REFERENCE_TIME interval = caps.MinFrameInterval > 0 ? caps.MinFrameInterval : defaultInterval;
            mode.maxFps = interval > 0 ? 1.0e7 / static_cast<double>(interval) : 0.0;
            mode.fourcc = subtypeToFourcc(type->subtype);
            mode.compressed = isCompressedFourcc(mode.fourcc);
            addMode(modes, std::move(mode));
        }

        freeMediaType(type);
    }

    std::sort(modes.begin(), modes.end(), [](const VideoMode& a, const VideoMode& b)
    {
        if(a.pixels() != b.pixels())
        {
            return a.pixels() > b.pixels();
        }
        return a.maxFps > b.maxFps;
    });

    return modes;
}

std::string toUpper(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

void classifyByDevicePath(const std::string& devicePath, CameraInfo& info)
{
    std::string lower = devicePath;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if(lower.empty())
    {
        info.transport = CameraTransport::Other;
        info.details = "DirectShow source filter";
        return;
    }
    if(lower.find("usb#") != std::string::npos || lower.find("usbvideo") != std::string::npos)
    {
        info.transport = CameraTransport::Usb;
    }
    else if(lower.find("pci#") != std::string::npos)
    {
        info.transport = CameraTransport::Pci;
    }
    else
    {
        info.transport = CameraTransport::Other;
    }

    const size_t vid = lower.find("vid_");
    const size_t pid = lower.find("pid_");
    if(vid != std::string::npos && pid != std::string::npos
        && vid + 8 <= lower.size() && pid + 8 <= lower.size())
    {
        info.details = "VID " + toUpper(devicePath.substr(vid + 4, 4))
                     + " PID " + toUpper(devicePath.substr(pid + 4, 4));
    }
}


} // namespace

std::vector<CameraInfo> DirectShowProvider::enumerate(std::string* error)
{
    std::vector<CameraInfo> cameras;

    ComScope com;
    if(!com.getUsable())
    {
        if(error)
        {
            *error = "COM initialisation failed";
        }
        return cameras;
    }

    ComPtr<ICreateDevEnum> deviceEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_ICreateDevEnum, deviceEnum.putVoid());
    if(FAILED(hr))
    {
        if(error)
        {
            *error = hresultText("CoCreateInstance(CLSID_SystemDeviceEnum)", hr);
        }
        return cameras;
    }

    ComPtr<IEnumMoniker> monikers;
    hr = deviceEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, monikers.put(), 0);
    if(hr == S_FALSE || !monikers)
    {
        return cameras;
    }
    if(FAILED(hr))
    {
        if(error)
        {
            *error = hresultText("CreateClassEnumerator(VideoInputDeviceCategory)", hr);
        }
        return cameras;
    }

    const bool openCvHasDirectShow = cv::videoio_registry::hasBackend(cv::CAP_DSHOW);

    ComPtr<IMoniker> moniker;
    int index = 0;
    while(monikers->Next(1, moniker.put(), nullptr) == S_OK)
    {
        CameraInfo info;
        info.api = name();
        info.captureApi = cv::CAP_DSHOW;
        info.captureIndex = index++;

        std::string devicePath;
        ComPtr<IPropertyBag> properties;
        if(SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag,
                                             properties.putVoid())))
        {
            info.name = readStringProperty(properties.get(), L"FriendlyName");
            devicePath = readStringProperty(properties.get(), L"DevicePath");
        }
        if(info.name.empty())
        {
            info.name = "Video device #" + std::to_string(info.captureIndex);
        }
        info.id = devicePath.empty() ? "dshow:" + info.name : devicePath;
        classifyByDevicePath(devicePath, info);

        ComPtr<IBaseFilter> filter;
        if(SUCCEEDED(moniker->BindToObject(nullptr, nullptr, IID_IBaseFilter,
                                            filter.putVoid())))
        {
            info.modes = queryModes(filter.get());
        }

        info.streamable = openCvHasDirectShow;
        if(!openCvHasDirectShow)
        {
            info.notStreamableReason = "this OpenCV build has no DirectShow backend (CAP_DSHOW)";
        }

        cameras.push_back(std::move(info));
    }

    return cameras;
}


} // namespace calib

#endif// _WIN32
