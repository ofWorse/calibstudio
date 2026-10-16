#ifndef CALIBSTUDIO_MAINWINDOW_H
#define CALIBSTUDIO_MAINWINDOW_H

#include "camera/cameracontrols.h"
#include "camera/camerainfo.h"
#include "camera/cameraprofile.h"
#include "processing/framefilter.h"

#include <QMainWindow>
#include <QVector>

#include <map>
#include <memory>
#include <string>

class QLabel;
class QListWidget;
class QTimer;

namespace calib
{

class CameraSettingsWidget;
class FilterPanel;
class CaptureThread;
class SettingsStore;
class VideoWidget;

} // namespace calib

class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    calib::VideoWidget* video = nullptr;
    QListWidget* cameraList = nullptr;
    calib::CameraSettingsWidget* settings = nullptr;
    calib::FilterPanel* filters = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* statsLabel = nullptr;
    calib::CaptureThread* capture = nullptr;
    QVector<calib::CameraInfo> cameras;

    // Saved settings. Changes are collected here and written to disk a moment
    // later, so dragging a slider does not rewrite the file on every step.
    std::unique_ptr<calib::SettingsStore> store;
    std::map<std::string, calib::CameraProfile> profiles;   // key: calib::profileKey()
    calib::CameraInfo activeCamera;                         // valid while activeKey is set
    std::string activeKey;
    bool profileDirty = false;
    bool filtersDirty = false;
    QTimer* saveTimer = nullptr;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow(void) override;

private:
    void onFrameReady(void);
    void onCamerasDiscovered(const QVector<calib::CameraInfo>& cameras);
    void onCameraOpened(int listIndex, const QString& description);
    void onCameraClosed(const QString& reason);
    void onStatusChanged(const QString& message);
    void onStatisticsUpdated(double captureFps, quint64 droppedFrames, double filterMs);
    void highlightActiveCamera(int listIndex);

    void onCameraConfigured(const calib::CameraInfo& camera, int modeIndex,
                            const QVector<calib::CameraControlInfo>& controls,
                            const QVector<calib::CameraControlState>& states, bool flipHorizontal, bool flipVertical);
    void onModeRequested(int modeIndex);
    void onFlipChanged(bool horizontal, bool vertical);
    void onControlRequested(calib::CameraControlId id, const calib::CameraControlState& state);
    void onResetRequested(void);
    void onFiltersChanged(const calib::FilterSettings& settings);
    calib::CameraProfile* activeProfile(void);
    void profileChanged(void);
    void saveNow(void);
};


#endif // CALIBSTUDIO_MAINWINDOW_H
