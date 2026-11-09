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
#include <set>
#include <string>

class QDockWidget;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMdiArea;
class QMdiSubWindow;
class QTimer;

namespace calib
{

class CameraSettingsWidget;
class CameraWindow;
class CaptureThread;
class DiscoveryThread;
class FilterPanel;
class SettingsStore;

} // namespace calib

// Every camera ticked in the camera list streams in its own thread into its
// own window. The settings and filter panels work on the active window.
class MainWindow : public QMainWindow
{
    Q_OBJECT

private:
    struct CameraSession
    {
        calib::CameraInfo camera;
        std::string profileKey;
        calib::CaptureThread* capture = nullptr;
        calib::CameraWindow* window = nullptr;
        calib::FilterSettings filters;
        bool configured = false;
        int modeIndex = -1;
        QVector<calib::CameraControlInfo> controls;
        QVector<calib::CameraControlState> states;
        bool flipHorizontal = false;
        bool flipVertical = false;
        QString status;
        QString statistics;
    };

    QMdiArea* views = nullptr;
    QListWidget* cameraList = nullptr;
    calib::CameraSettingsWidget* settings = nullptr;
    calib::FilterPanel* filters = nullptr;
    QDockWidget* settingsDock = nullptr;
    QDockWidget* filtersDock = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* statsLabel = nullptr;
    calib::DiscoveryThread* discovery = nullptr;

    QVector<calib::CameraInfo> cameras;
    std::map<std::string, std::unique_ptr<CameraSession>> sessions;
    std::string activeId;
    std::set<std::string> enabledIds;
    bool enabledLoaded = false;
    bool updatingPanels = false;
    bool updatingList = false;
    bool autoTile = true;

    std::unique_ptr<calib::SettingsStore> store;
    std::map<std::string, calib::CameraProfile> profiles;
    std::map<std::string, QString> profileNames;
    std::set<std::string> dirtyProfiles;
    std::map<std::string, calib::FilterSettings> dirtyFilters;
    bool enabledDirty = false;
    QTimer* saveTimer = nullptr;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow(void) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onCamerasDiscovered(const QVector<calib::CameraInfo>& cameras);
    void rebuildCameraList(void);
    void onCameraItemChanged(QListWidgetItem* item);
    void setCameraEnabled(const std::string& id, bool enabled);
    void startSession(const calib::CameraInfo& camera);
    void stopSession(const std::string& id);
    CameraSession* findSession(const std::string& id);
    CameraSession* activeSession(void);

    void tileWindows(void);
    void expandWindows(void);
    void highlightActiveCamera(void);
    void onWindowActivated(QMdiSubWindow* window);
    void showActiveSession(void);
    void showStatus(void);

    void onModeRequested(int modeIndex);
    void onFlipChanged(bool horizontal, bool vertical);
    void onControlRequested(calib::CameraControlId id, const calib::CameraControlState& state);
    void onResetRequested(void);
    void onAutoTargetChanged(int brightness);
    void onFiltersChanged(const calib::FilterSettings& filterSettings);

    calib::CameraProfile* activeProfile(void);
    void profileChanged(CameraSession& session);
    void scheduleSave(void);
    void saveNow(void);
};


#endif // CALIBSTUDIO_MAINWINDOW_H
