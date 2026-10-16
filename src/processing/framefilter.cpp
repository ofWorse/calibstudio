#include "processing/framefilter.h"

#include <algorithm>

namespace calib
{

namespace
{


int toOdd(int value, int minimum)
{
    value = std::max(value, minimum);
    return value % 2 == 0 ? value + 1 : value;
}

int toOpenCvColorMap(FalseColorMap map)
{
    switch(map)
    {
    case FalseColorMap::Jet:     return cv::COLORMAP_JET;
    case FalseColorMap::Turbo:   return cv::COLORMAP_TURBO;
    case FalseColorMap::Viridis: return cv::COLORMAP_VIRIDIS;
    case FalseColorMap::Inferno: return cv::COLORMAP_INFERNO;
    case FalseColorMap::Hot:     return cv::COLORMAP_HOT;
    case FalseColorMap::None:    break;
    }
    return cv::COLORMAP_JET;
}


} // namespace

const char* toString(StructureFilter filter)
{
    switch(filter)
    {
    case StructureFilter::None:              return "None";
    case StructureFilter::FixedThreshold:    return "Threshold (fixed)";
    case StructureFilter::OtsuThreshold:     return "Threshold (Otsu, automatic)";
    case StructureFilter::AdaptiveThreshold: return "Adaptive threshold";
    case StructureFilter::CannyEdges:        return "Edges (Canny)";
    }
    return "None";
}

const char* toString(FalseColorMap map)
{
    switch(map)
    {
    case FalseColorMap::None:    return "None";
    case FalseColorMap::Jet:     return "Jet";
    case FalseColorMap::Turbo:   return "Turbo";
    case FalseColorMap::Viridis: return "Viridis";
    case FalseColorMap::Inferno: return "Inferno";
    case FalseColorMap::Hot:     return "Hot";
    }
    return "None";
}

bool FilterSettings::isIdentity(void) const
{
    return !grayscale && !equalize && blurKernel < 3 && sharpenPercent <= 0 && structure == StructureFilter::None
        && !negative && colorMap == FalseColorMap::None;
}

void FrameFilter::setSettings(const FilterSettings& newSettings)
{
    settings = newSettings;
    settings.claheClipLimit = std::clamp(settings.claheClipLimit, 0.1, 40.0);
    settings.blurKernel = settings.blurKernel < 3 ? 0 : toOdd(settings.blurKernel, 3);
    settings.sharpenPercent = std::max(0, settings.sharpenPercent);
    settings.threshold = std::clamp(settings.threshold, 0, 255);
    settings.adaptiveBlockSize = toOdd(settings.adaptiveBlockSize, 3);
    settings.cannyLow = std::max(0, settings.cannyLow);
    settings.cannyHigh = std::max(settings.cannyLow, settings.cannyHigh);
}

const cv::Mat& FrameFilter::apply(const cv::Mat& input)
{
    if(input.empty() || settings.isIdentity())
    {
        return input;
    }

    const cv::Mat* current = &input;
    int nextBuffer = 0;
    const auto output = [this, &nextBuffer]() -> cv::Mat&
    {
        cv::Mat& target = buffers[nextBuffer];
        nextBuffer ^= 1;
        return target;
    };

    if(current->depth() != CV_8U)
    {
        cv::Mat& out = output();
        current->convertTo(out, CV_8U, current->depth() == CV_16U ? 1.0 / 256.0 : 1.0);
        current = &out;
    }
    if(current->channels() == 4)
    {
        cv::Mat& out = output();
        cv::cvtColor(*current, out, cv::COLOR_BGRA2BGR);
        current = &out;
    }

    const bool needsGray = settings.grayscale || settings.structure != StructureFilter::None
        || settings.colorMap != FalseColorMap::None;
    if(needsGray && current->channels() == 3)
    {
        cv::Mat& out = output();
        cv::cvtColor(*current, out, cv::COLOR_BGR2GRAY);
        current = &out;
    }

    if(settings.equalize)
    {
        if(!clahe)
        {
            clahe = cv::createCLAHE();
        }
        clahe->setClipLimit(settings.claheClipLimit);

        cv::Mat& out = output();
        if(current->channels() == 1)
        {
            clahe->apply(*current, out);
        }
        else
        {
            cv::cvtColor(*current, scratch, cv::COLOR_BGR2YCrCb);
            cv::extractChannel(scratch, lightness, 0);
            clahe->apply(lightness, lightnessEqualized);
            cv::insertChannel(lightnessEqualized, scratch, 0);
            cv::cvtColor(scratch, out, cv::COLOR_YCrCb2BGR);
        }
        current = &out;
    }

    if(settings.blurKernel >= 3)
    {
        cv::Mat& out = output();
        cv::GaussianBlur(*current, out, cv::Size(settings.blurKernel, settings.blurKernel), 0);
        current = &out;
    }

    if(settings.sharpenPercent > 0)
    {
        const double amount = settings.sharpenPercent / 100.0;
        cv::GaussianBlur(*current, scratch, cv::Size(0, 0), 1.5);
        cv::Mat& out = output();
        cv::addWeighted(*current, 1.0 + amount, scratch, -amount, 0.0, out);
        current = &out;
    }

    if(settings.structure != StructureFilter::None)
    {
        cv::Mat& out = output();
        switch(settings.structure)
        {
        case StructureFilter::FixedThreshold:
            cv::threshold(*current, out, settings.threshold, 255, cv::THRESH_BINARY);
            break;
        case StructureFilter::OtsuThreshold:
            cv::threshold(*current, out, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            break;
        case StructureFilter::AdaptiveThreshold:
            cv::adaptiveThreshold(*current, out, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                                  settings.adaptiveBlockSize, settings.adaptiveOffset);
            break;
        case StructureFilter::CannyEdges: cv::Canny(*current, out, settings.cannyLow, settings.cannyHigh); break;
        case StructureFilter::None:       break;
        }
        current = &out;
    }

    if(settings.negative)
    {
        cv::Mat& out = output();
        cv::bitwise_not(*current, out);
        current = &out;
    }

    if(settings.colorMap != FalseColorMap::None)
    {
        cv::Mat& out = output();
        cv::applyColorMap(*current, out, toOpenCvColorMap(settings.colorMap));
        current = &out;
    }

    return *current;
}


} // namespace calib
