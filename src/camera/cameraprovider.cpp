#include "camera/cameraprovider.h"

#include "camera/gige/gigevisionprovider.h"

#ifdef _WIN32
#include "camera/directshow/directshowprovider.h"
#else
#include "camera/generic/opencvprobeprovider.h"
#endif

namespace calib
{

std::vector<std::unique_ptr<ICameraProvider>> createDefaultProviders(void)
{
    std::vector<std::unique_ptr<ICameraProvider>> providers;
#ifdef _WIN32
    // USB/UVC webcams, capture cards and vendor cameras with a DirectShow driver.
    providers.push_back(std::make_unique<DirectShowProvider>());
#else
    // TMP Placeholder until a native V4L2 provider exists.
#endif
    // GigE Vision cameras on every local network interface.
    providers.push_back(std::make_unique<GigEVisionProvider>());
    return providers;
}


} // namespace calib
