#ifndef CALIBSTUDIO_CAPTURETHREAD_H
#define CALIBSTUDIO_CAPTURETHREAD_H

#include "camera/cameracontrols.h"
#include "camera/cameraprofile.h"
#include "camera/camerainfo.h"
#include "processing/framefilter.h"

#include <QImage>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>
#include <map>
#include <mutex>

Q_DECLARE_METATYPE(calib::CameraInfo)
Q_DECLARE_METATYPE(calib::CameraControlId)
Q_DECLARE_METATYPE(calib::CameraControlInfo)
Q_DECLARE_METATYPE(calib::CameraControlState)

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
    std::atomic<bool> cameraOpen{false};
    std::atomic<bool> framePending{false};
    std::atomic<quint64> droppedFrames{0};

    QMutex frameMutex;
    QImage latestFrame;

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
    std::map<std::string, CameraProfile> savedProfiles;

public:
    explicit CaptureThread(QObject* parent = nullptr);
    ~CaptureThread(void) override;

    void stop(void);

    bool isCameraOpen(void) const
    {
        return cameraOpen.load();
    }

    QImage takeLatestFrame(void);

    void setFlip(bool horizontal, bool vertical);
    void setControl(calib::CameraControlId id, const calib::CameraControlState& state);
    void resetControls(void);
    void selectMode(int modeIndex);
    void setFilterSettings(const calib::FilterSettings& settings);
    void setSavedProfiles(const std::map<std::string, CameraProfile>& profiles);
    void setSavedProfile(const std::string& key, const CameraProfile& profile);

signals:
    void statusChanged(const QString& message);
    void camerasDiscovered(const QVector<calib::CameraInfo>& cameras);
    void cameraOpened(int listIndex, const QString& description);
    void cameraClosed(const QString& reason);
    void frameReady(void);
    void statisticsUpdated(double captureFps, quint64 droppedFrames, double filterMs);
    void cameraConfigured(const calib::CameraInfo& camera, int modeIndex,
                          const QVector<calib::CameraControlInfo>& controls,
                          const QVector<calib::CameraControlState>& states, bool flipHorizontal, bool flipVertical);
    void controlChanged(calib::CameraControlId id, const calib::CameraControlState& state);

protected:
    void run(void) override;

private:
    QVector<CameraInfo> discoverCameras(void);
    QString streamUntilStopped(CameraDevice& device, int listIndex);
    bool applyPendingCommands(CameraDevice& device, int listIndex, QString* failure);
    void announceCamera(const CameraDevice& device, int listIndex);
    int savedModeIndex(const CameraInfo& camera);
    void restoreProfile(CameraDevice& device);
    void publishFrame(const cv::Mat& frame);
    void sleepInterruptible(int milliseconds);
};


} // namespace calib

#endif // CALIBSTUDIO_CAPTURETHREAD_H
