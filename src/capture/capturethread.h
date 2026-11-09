#ifndef CALIBSTUDIO_CAPTURETHREAD_H
#define CALIBSTUDIO_CAPTURETHREAD_H

#include "camera/cameracontrols.h"
#include "camera/cameraprofile.h"
#include "camera/camerainfo.h"
#include "capture/capturetypes.h"
#include "processing/autoexposure.h"
#include "processing/framefilter.h"

#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>
#include <map>
#include <mutex>

namespace cv
{

class Mat;

} // namespace cv

namespace calib
{

class CameraDevice;


class CaptureThread : public QThread
{
    Q_OBJECT

private:
    std::mutex cameraMutex;
    CameraInfo camera;

    std::atomic<bool> cameraOpen{false};
    std::atomic<bool> framePending{false};
    std::atomic<quint64> droppedFrames{0};

    QMutex frameMutex;
    QImage latestFrame;
    QSize latestSourceSize;
    std::atomic<int> displayWidth{0};
    std::atomic<int> displayHeight{0};
    std::atomic<double> displayRatio{1.0};

    std::atomic<bool> flipHorizontal{false};
    std::atomic<bool> flipVertical{false};

    struct PendingCommands
    {
        std::map<CameraControlId, CameraControlState> controls;
        bool resetControls = false;
        int modeIndex = -1;
    };
    std::mutex commandMutex;
    PendingCommands pendingCommands;
    std::atomic<bool> commandsPending{false};

    std::mutex filterMutex;
    FilterSettings requestedFilters;
    std::atomic<bool> filtersChanged{false};
    FrameFilter frameFilter;

    std::mutex profileMutex;
    CameraProfile profile;

    AutoExposure autoExposure;
    std::atomic<int> autoTarget{110};
    std::atomic<bool> autoTargetChanged{false};
    std::map<CameraControlId, int> reportedAutoValues;

public:
    explicit CaptureThread(const CameraInfo& camera, QObject* parent = nullptr);
    ~CaptureThread(void) override;

    void stop(void);

    bool isCameraOpen(void) const
    {
        return cameraOpen.load();
    }

    void updateCamera(const calib::CameraInfo& camera);
    CameraInfo getCamera(void);

    QImage takeLatestFrame(QSize* sourceSize = nullptr);
    void setDisplaySize(int width, int height, double pixelRatio);

    void setFlip(bool horizontal, bool vertical);
    void setControl(calib::CameraControlId id, const calib::CameraControlState& state);
    void resetControls(void);
    void selectMode(int modeIndex);
    void setFilterSettings(const calib::FilterSettings& settings);
    void setProfile(const calib::CameraProfile& profile);
    // Mean brightness (0..255) that Once / Continuous exposure and gain aim for.
    void setAutoTarget(int brightness);

signals:
    void statusChanged(const QString& message);
    void cameraOpened(const QString& description);
    void cameraClosed(const QString& reason);
    void frameReady(void);
    void statisticsUpdated(double captureFps, quint64 droppedFrames, double filterMs, quint64 incompleteFrames);
    void cameraConfigured(const calib::CameraInfo& camera, int modeIndex,
                          const QVector<calib::CameraControlInfo>& controls,
                          const QVector<calib::CameraControlState>& states, bool flipHorizontal, bool flipVertical);
    void controlChanged(calib::CameraControlId id, const calib::CameraControlState& state);
    void autoAdjustFinished(calib::CameraControlId id, const calib::CameraControlState& state);

protected:
    void run(void) override;

private:
    bool openCamera(CameraDevice& device, QString* error);
    QString streamUntilStopped(CameraDevice& device);
    bool applyPendingCommands(CameraDevice& device, QString* failure);
    void announceCamera(const CameraDevice& device);
    void applyControl(ICameraControls* controls, CameraControlId id, const CameraControlState& state);
    void runAutoExposure(CameraDevice& device, const cv::Mat& frame);
    void reportCameraAuto(CameraDevice& device);
    int savedModeIndex(const CameraInfo& target);
    void restoreProfile(CameraDevice& device);
    void publishFrame(const cv::Mat& frame);
    void sleepInterruptible(int milliseconds);
};


} // namespace calib

#endif // CALIBSTUDIO_CAPTURETHREAD_H
