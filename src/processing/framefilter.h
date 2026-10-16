#ifndef CALIBSTUDIO_FRAMEFILTER_H
#define CALIBSTUDIO_FRAMEFILTER_H

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

namespace calib
{


enum class StructureFilter
{
    None,
    FixedThreshold,
    OtsuThreshold,
    AdaptiveThreshold,
    CannyEdges,
};

enum class FalseColorMap
{
    None,
    Jet,
    Turbo,
    Viridis,
    Inferno,
    Hot,
};

const char* toString(StructureFilter filter);
const char* toString(FalseColorMap map);


struct FilterSettings
{
    bool grayscale = false;

    bool equalize = false;
    double claheClipLimit = 2.0;

    int blurKernel = 0;
    int sharpenPercent = 0;

    StructureFilter structure = StructureFilter::None;
    int threshold = 128;
    int adaptiveBlockSize = 31;
    int adaptiveOffset = 5;
    int cannyLow = 50;
    int cannyHigh = 150;

    bool negative = false;
    FalseColorMap colorMap = FalseColorMap::None;

    bool isIdentity(void) const;
};


class FrameFilter
{
private:
    FilterSettings settings;
    cv::Mat buffers[2];
    cv::Mat scratch;
    cv::Mat lightness;
    cv::Mat lightnessEqualized;
    cv::Ptr<cv::CLAHE> clahe;

public:
    void setSettings(const FilterSettings& newSettings);

    const FilterSettings& getSettings(void) const
    {
        return settings;
    }

    const cv::Mat& apply(const cv::Mat& input);
};


} // namespace calib

#endif // CALIBSTUDIO_FRAMEFILTER_H
