#ifndef CALIBSTUDIO_DIRECTSHOWPROVIDER_H
#define CALIBSTUDIO_DIRECTSHOWPROVIDER_H

#include "cameraprovider.h"

namespace calib
{

// Windows only
class DirectShowProvider final : public ICameraProvider
{
public:
    const char* name(void) const override
    { return "DirectShow"; }
    std::vector<CameraInfo> enumerate(std::string* error) override;
};


} // namespace calib

#endif // CALIBSTUDIO_DIRECTSHOWPROVIDER_H
