#ifndef CALIBSTUDIO_AUTOEXPOSURE_H
#define CALIBSTUDIO_AUTOEXPOSURE_H

#include "camera/cameracontrols.h"

#include <opencv2/core/mat.hpp>

#include <utility>
#include <vector>

namespace calib
{

class AutoExposure
{
private:
    struct Channel
    {
        bool active = false;
        ControlMode mode = ControlMode::Manual;
        CameraControlInfo info;
        int value = 0;
    };

    Channel exposure;
    Channel gain;
    int target = 110;
    int settleFrames = 0;
    int steps = 0;
    double brightness = -1.0;

public:
    struct Result
    {
        std::vector<std::pair<CameraControlId, int>> changes;
        std::vector<CameraControlId> finished;
    };

    void setTarget(int brightness);
    int getTarget(void) const
    {
        return target;
    }
    double getBrightness(void) const
    {
        return brightness;
    }

    void start(const CameraControlInfo& control, int currentValue, ControlMode mode);
    void stop(CameraControlId id);
    void stopAll(void);
    bool isActive(void) const;
    bool isActive(CameraControlId id) const;
    ControlMode modeOf(CameraControlId id) const;

    Result update(const cv::Mat& frame);

private:
    Channel* channel(CameraControlId id);
    const Channel* channel(CameraControlId id) const;
    double apply(Channel& channel, double stops);
};

double meanBrightness(const cv::Mat& frame);


} // namespace calib

#endif // CALIBSTUDIO_AUTOEXPOSURE_H
