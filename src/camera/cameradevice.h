#ifndef CALIBSTUDIO_CAMERADEVICE_H
#define CALIBSTUDIO_CAMERADEVICE_H

#include "camera/cameracontrols.h"
#include "camera/camerainfo.h"

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace calib
{

class GigEDevice;

// One open camera: through OpenCV (USB/DirectShow) or through CalibStudio's
// own GigE Vision stack.
class CameraDevice
{
private:
    cv::VideoCapture capture;
    std::unique_ptr<GigEDevice> gige;
    std::string lastError;
    CameraInfo info;
    VideoMode activeMode;
    int activeModeIndex = -1;
    std::unique_ptr<ICameraControls> controls;

public:
    CameraDevice(void);
    ~CameraDevice(void);
    CameraDevice(const CameraDevice&) = delete;
    CameraDevice& operator=(const CameraDevice&) = delete;

    bool open(const CameraInfo& info, std::string* error, int modeIndex = -1);
    bool isOpened(void) const;
    void close(void);
    bool read(cv::Mat& frame);

    const CameraInfo& getInfo(void) const
    {
        return info;
    }
    const VideoMode& getActiveMode(void) const
    {
        return activeMode;
    }
    int getActiveModeIndex(void) const
    {
        return activeModeIndex;
    }
    ICameraControls* getControls(void) const
    {
        return controls.get();
    }
    std::string backendName(void) const;
    std::uint64_t getIncompleteFrames(void) const;
    const std::string& getLastError(void) const
    {
        return lastError;
    }

private:
    bool openGigE(const CameraInfo& info, std::string* error);
    void applyMode(const VideoMode& mode, int captureApi);
};

const VideoMode* pickLiveMode(const std::vector<VideoMode>& modes);


} // namespace calib

#endif // CALIBSTUDIO_CAMERADEVICE_H
