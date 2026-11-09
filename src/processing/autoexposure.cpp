#include "processing/autoexposure.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace calib
{

namespace
{

constexpr double kTolerance = 0.12;
constexpr double kDamping = 0.7;
constexpr int kSettleFrames = 3;
constexpr int kMaxOnceSteps = 30;
constexpr int kMeasureWidth = 160;


double linearOffset(const CameraControlInfo& info)
{
    if(info.unit == ControlUnit::Raw)
    {
        return std::max(1.0, (info.maximum - info.minimum) * 0.05) - info.minimum;
    }
    return 1.0 - info.minimum;
}

double stopsBetween(const CameraControlInfo& info, int from, int to)
{
    switch(info.unit)
    {
    case ControlUnit::Log2Seconds:  return to - from;
    case ControlUnit::TenthDecibel: return (to - from) / 60.206;
    case ControlUnit::Microseconds:
    case ControlUnit::Raw:          {
        const double offset = linearOffset(info);
        return std::log2((to + offset) / (from + offset));
    }
    }
    return 0.0;
}

int valueAfter(const CameraControlInfo& info, int from, double stops)
{
    double wanted = from;
    switch(info.unit)
    {
    case ControlUnit::Log2Seconds:  wanted = std::round(from + stops); break;
    case ControlUnit::TenthDecibel: wanted = from + stops * 60.206; break;
    case ControlUnit::Microseconds:
    case ControlUnit::Raw:          {
        const double offset = linearOffset(info);
        wanted = (from + offset) * std::exp2(stops) - offset;
        break;
    }
    }
    const double clamped = std::clamp(wanted, static_cast<double>(info.minimum), static_cast<double>(info.maximum));
    const int step = std::max(1, info.step);
    const int snapped = info.minimum + static_cast<int>(std::lround((clamped - info.minimum) / step)) * step;
    return std::clamp(snapped, info.minimum, info.maximum);
}

} // namespace

double meanBrightness(const cv::Mat& frame)
{
    if(frame.empty())
    {
        return -1.0;
    }
    cv::Mat small;
    const double scale = std::min(1.0, static_cast<double>(kMeasureWidth) / frame.cols);
    cv::resize(frame, small, cv::Size(), scale, scale, cv::INTER_AREA);
    const cv::Scalar mean = cv::mean(small);
    switch(small.channels())
    {
    case 1:  return mean[0];
    case 3:
    case 4:  return 0.114 * mean[0] + 0.587 * mean[1] + 0.299 * mean[2];   // BGR luma
    default: return mean[0];
    }
}

void AutoExposure::setTarget(int value)
{
    target = std::clamp(value, 10, 245);
}

void AutoExposure::start(const CameraControlInfo& control, int currentValue, ControlMode mode)
{
    Channel* selected = channel(control.id);
    if(!selected || (mode != ControlMode::Once && mode != ControlMode::Continuous))
    {
        stop(control.id);
        return;
    }
    selected->active = true;
    selected->mode = mode;
    selected->info = control;
    selected->value = std::clamp(currentValue, control.minimum, control.maximum);
    settleFrames = 0;
    steps = 0;
}

void AutoExposure::stop(CameraControlId id)
{
    if(Channel* selected = channel(id))
    {
        selected->active = false;
        selected->mode = ControlMode::Manual;
    }
}

void AutoExposure::stopAll(void)
{
    stop(CameraControlId::Exposure);
    stop(CameraControlId::Gain);
}

bool AutoExposure::isActive(void) const
{
    return exposure.active || gain.active;
}

bool AutoExposure::isActive(CameraControlId id) const
{
    const Channel* selected = channel(id);
    return selected && selected->active;
}

ControlMode AutoExposure::modeOf(CameraControlId id) const
{
    const Channel* selected = channel(id);
    return selected && selected->active ? selected->mode : ControlMode::Manual;
}

AutoExposure::Result AutoExposure::update(const cv::Mat& frame)
{
    Result result;
    if(!isActive())
    {
        return result;
    }
    if(settleFrames > 0)
    {
        --settleFrames;
        return result;
    }

    brightness = meanBrightness(frame);
    if(brightness < 0)
    {
        return result;
    }
    double error = std::log2(target / std::max(brightness, 1.0));
    if(brightness > 250.0)
    {
        error = std::min(error, -1.5);   // clipped: the real overexposure is unknown, step down fast
    }

    const bool onTarget = std::fabs(error) < kTolerance;
    bool changed = false;
    if(!onTarget)
    {
        Channel* first = error > 0 ? &exposure : &gain;
        Channel* second = error > 0 ? &gain : &exposure;
        double remaining = error * kDamping;
        for(Channel* current : {first, second})
        {
            if(current->active && std::fabs(remaining) > 1e-3)
            {
                const int before = current->value;
                remaining -= apply(*current, remaining);
                if(current->value != before)
                {
                    result.changes.emplace_back(current->info.id, current->value);
                    changed = true;
                }
            }
        }
    }

    if(changed)
    {
        settleFrames = kSettleFrames;
        ++steps;
    }

    if(onTarget || !changed || steps >= kMaxOnceSteps)
    {
        for(Channel* current : {&exposure, &gain})
        {
            if(current->active && current->mode == ControlMode::Once)
            {
                result.finished.push_back(current->info.id);
                current->active = false;
                current->mode = ControlMode::Manual;
            }
        }
        if(onTarget)
        {
            steps = 0;
        }
    }
    return result;
}

AutoExposure::Channel* AutoExposure::channel(CameraControlId id)
{
    return id == CameraControlId::Exposure ? &exposure : (id == CameraControlId::Gain ? &gain : nullptr);
}

const AutoExposure::Channel* AutoExposure::channel(CameraControlId id) const
{
    return id == CameraControlId::Exposure ? &exposure : (id == CameraControlId::Gain ? &gain : nullptr);
}

double AutoExposure::apply(Channel& channel, double stops)
{
    const int next = valueAfter(channel.info, channel.value, stops);
    const double achieved = stopsBetween(channel.info, channel.value, next);
    channel.value = next;
    return achieved;
}


} // namespace calib
