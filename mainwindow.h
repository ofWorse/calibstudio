#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "camera/camerainfo.h"

#include <QMainWindow>
#include <QVector>

class QLabel;
class QListWidget;

namespace calib
{

class CaptureThread;
class VideoWidget;

}

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    calib::VideoWidget *video = nullptr;
    QListWidget *cameraList = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *statsLabel = nullptr;
    calib::CaptureThread *capture = nullptr;
    QVector<calib::CameraInfo> cameras;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow(void) override;

private:
    void onFrameReady(void);
    void onCamerasDiscovered(const QVector<calib::CameraInfo> &cameras);
    void onCameraOpened(int listIndex, const QString &description);
    void onCameraClosed(const QString &reason);
    void onStatusChanged(const QString &message);
    void onStatisticsUpdated(double captureFps, quint64 droppedFrames);
    void highlightActiveCamera(int listIndex);
};


#endif // MAINWINDOW_H
