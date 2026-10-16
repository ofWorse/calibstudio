#ifndef CALIBSTUDIO_DSHOWCOMMON_H
#define CALIBSTUDIO_DSHOWCOMMON_H


#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>

#include <cstdio>
#include <string>

namespace calib
{

namespace dshow
{


template <class T>
class ComPtr
{
private:
    T* ptr = nullptr;

public:
    ComPtr(void) = default;
    ~ComPtr(void)
    {
        reset();
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    T* get(void) const
    {
        return ptr;
    }

    T* operator->(void) const
    {
        return ptr;
    }

    explicit operator bool(void) const
    {
        return ptr != nullptr;
    }

    // Releases the current object and returns the slot for an out-parameter
    T** put(void)
    {
        reset();
        return &ptr;
    }

    void** putVoid(void)
    {
        return reinterpret_cast<void**>(put());
    }

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

    bool getUsable(void) const
    {
        return usable;
    }
};


inline std::string hresultText(const char* what, HRESULT hr)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    return buffer;
}

inline std::string toUtf8(const wchar_t* text)
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
    result.resize(static_cast<size_t>(size - 1)); // drop the terminating NUL

    return result;
}

inline std::string readStringProperty(IPropertyBag* bag, const wchar_t* key)
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


} // namespace dshow

} // namespace calib

#endif // _WIN32
#endif // CALIBSTUDIO_DSHOWCOMMON_H
