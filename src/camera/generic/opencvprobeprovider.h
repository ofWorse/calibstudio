#ifndef CALIBSTUDIO_OPENCVPROBEPROVIDER_H
#define CALIBSTUDIO_OPENCVPROBEPROVIDER_H

#include "camera/cameraprovider.h"

namespace calib
{

class OpenCvProbeProvider final : public ICameraProvider
{
private:
    int maxIndex;

public:
    explicit OpenCvProbeProvider(int maxIndex = 3)
        : maxIndex(maxIndex)
    {
    }

    const char* name(void) const override
    {
        return "OpenCV probe";
    }
    std::vector<CameraInfo> enumerate(std::string* error) override;
};

} // namespace calib

#endif // CALIBSTUDIO_OPENCVPROBEPROVIDER_H
