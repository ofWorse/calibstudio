#ifndef CALIBSTUDIO_GIGEVISIONPROVIDER_H
#define CALIBSTUDIO_GIGEVISIONPROVIDER_H

#include "cameraprovider.h"

#include <cstdint>

namespace calib
{

class GigEVisionProvider final : public ICameraProvider
{
private:
    int replyTimeoutMs;
    std::uint16_t requestId = 0;

public:
    explicit GigEVisionProvider(int replyTimeoutMs = 500);

    const char* name(void) const override
    { return "GigE Vision"; }
    std::vector<CameraInfo> enumerate(std::string* error) override;
};

} // namespace calib

#endif // CALIBSTUDIO_GIGEVISIONPROVIDER_H
