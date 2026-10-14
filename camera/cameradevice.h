#ifndef CALIBSTUDIO_CAMERADEVICE_H
#define CALIBSTUDIO_CAMERADEVICE_H

#include "camerainfo.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include <string>

namespace calib
{

class CameraDevice
{
private:
    cv::VideoCapture capture;
    CameraInfo info;
    VideoMode activeMode;

public:
    CameraDevice(void) = default;
    ~CameraDevice(void);
    CameraDevice(const CameraDevice&) = delete;
    CameraDevice& operator=(const CameraDevice&) = delete;

    bool open(const CameraInfo& info, std::string* error);
    bool isOpened(void) const;
    void close(void);
    bool read(cv::Mat& frame);

    const CameraInfo& getInfo(void) const
    { return info; }
    const VideoMode& getActiveMode(void) const
    { return activeMode; }
    std::string backendName(void) const;

private:
    void applyMode(const VideoMode& mode, int captureApi);
};

const VideoMode* pickLiveMode(const std::vector<VideoMode>& modes);


} // namespace calib

#endif // CALIBSTUDIO_CAMERADEVICE_H
