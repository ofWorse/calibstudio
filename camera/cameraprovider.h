#ifndef CALIBSTUDIO_CAMERAPROVIDER_H
#define CALIBSTUDIO_CAMERAPROVIDER_H

#include "camerainfo.h"

#include <memory>
#include <string>
#include <vector>

namespace calib
{


class ICameraProvider
{
public:
    virtual ~ICameraProvider(void) = default;

    virtual const char* name(void) const = 0;

    virtual std::vector<CameraInfo> enumerate(std::string* error) = 0;
};


std::vector<std::unique_ptr<ICameraProvider>> createDefaultProviders(void);


} // namespace calib

#endif // CALIBSTUDIO_CAMERAPROVIDER_H
