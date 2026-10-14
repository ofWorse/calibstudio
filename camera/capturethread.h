#ifndef CALIBSTUDIO_CAPTURETHREAD_H
#define CALIBSTUDIO_CAPTURETHREAD_H

#include "camerainfo.h"

#include <QImage>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

Q_DECLARE_METATYPE(calib::CameraInfo)

namespace cv
{

class Mat;

}

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

public:
    explicit CaptureThread(QObject* parent = nullptr);
    ~CaptureThread(void) override;

    void stop(void);

    bool isCameraOpen(void) const
    { return cameraOpen.load(); }

    QImage takeLatestFrame(void);

signals:
    void statusChanged(const QString& message);
    void camerasDiscovered(const QVector<calib::CameraInfo>& cameras);
    void cameraOpened(int listIndex, const QString& description);
    void cameraClosed(const QString& reason);
    void frameReady(void);
    void statisticsUpdated(double captureFps, quint64 droppedFrames);

protected:
    void run(void) override;

private:
    QVector<CameraInfo> discoverCameras(void);
    QString streamUntilStopped(CameraDevice& device);
    void publishFrame(const cv::Mat& frame);
    void sleepInterruptible(int milliseconds);
};


} // namespace calib

#endif // CALIBSTUDIO_CAPTURETHREAD_H
