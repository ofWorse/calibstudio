#ifndef CALIBSTUDIO_GIGEVISIONPROVIDER_H
#define CALIBSTUDIO_GIGEVISIONPROVIDER_H

#include "camera/cameraprovider.h"

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace calib
{

namespace gvcp
{
struct DeviceInfo;
} // namespace gvcp

// Finds GigE Vision cameras with a GVCP discovery broadcast on every network card.
class GigEVisionProvider final : public ICameraProvider
{
private:
    struct Probe;
    struct Reply;

    int replyTimeoutMs;
    bool fixAddresses;
    std::uint16_t requestId = 0;

public:
    explicit GigEVisionProvider(int replyTimeoutMs = 500, bool fixAddresses = true);
    ~GigEVisionProvider(void) override;

    const char* name(void) const override
    {
        return "GigE Vision";
    }
    std::vector<CameraInfo> enumerate(std::string* error) override;

private:
    std::uint16_t nextRequestId(void);
    std::vector<Probe> openProbes(void);
    std::vector<Reply> discover(std::vector<Probe>& probes);
    bool forceIp(Probe& probe, const gvcp::DeviceInfo& device, std::uint32_t ip, std::string* problem);
};

std::uint32_t chooseCameraAddress(std::uint32_t hostIp, std::uint32_t netmask, const std::set<std::uint32_t>& used);

} // namespace calib

#endif // CALIBSTUDIO_GIGEVISIONPROVIDER_H
