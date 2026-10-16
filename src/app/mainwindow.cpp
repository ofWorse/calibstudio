#include "app/mainwindow.h"

#include "capture/capturethread.h"
#include "settings/settingsstore.h"
#include "ui/camerasettingswidget.h"
#include "ui/filterpanel.h"
#include "ui/videowidget.h"

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace
{


QString cameraTooltip(const calib::CameraInfo& camera)
{
    QStringList lines;
    lines << QString::fromStdString(camera.name) << QObject::tr("Found via %1").arg(QString::fromStdString(camera.api));

    if(!camera.details.empty())
    {
        lines << QString::fromStdString(camera.details);
    }
    if(!camera.streamable)
    {
        lines << QObject::tr("Cannot stream: %1").arg(QString::fromStdString(camera.notStreamableReason));
    }

    constexpr int kMaxModesShown = 12;
    const int shown = std::min<int>(static_cast<int>(camera.modes.size()), kMaxModesShown);
    for(int i = 0; i < shown; ++i)
    {
        lines << QStringLiteral("  ") + QString::fromStdString(calib::describe(camera.modes[static_cast<size_t>(i)]));
    }

    if(static_cast<int>(camera.modes.size()) > shown)
    {
        lines << QObject::tr("  ... %1 more").arg(static_cast<int>(camera.modes.size()) - shown);
    }

    return lines.join(QLatin1Char('\n'));
}


} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("CalibStudio"));

    video = new calib::VideoWidget(this);
    video->setMessage(tr("Starting..."));
    setCentralWidget(video);

    cameraList = new QListWidget(this);
    cameraList->setSelectionMode(QAbstractItemView::NoSelection);
    auto* dock = new QDockWidget(tr("Cameras"), this);
    dock->setObjectName(QStringLiteral("camerasDock"));
    dock->setWidget(cameraList);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    settings = new calib::CameraSettingsWidget(this);
    auto* settingsScroll = new QScrollArea(this);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setWidget(settings);
    auto* settingsDock = new QDockWidget(tr("Camera settings"), this);
    settingsDock->setObjectName(QStringLiteral("cameraSettingsDock"));
    settingsDock->setWidget(settingsScroll);
    addDockWidget(Qt::RightDockWidgetArea, settingsDock);
    resizeDocks({dock, settingsDock}, {140, 620}, Qt::Vertical);

    filters = new calib::FilterPanel(this);
    auto* filtersScroll = new QScrollArea(this);
    filtersScroll->setWidgetResizable(true);
    filtersScroll->setFrameShape(QFrame::NoFrame);
    filtersScroll->setWidget(filters);
    auto* filtersDock = new QDockWidget(tr("Image filters"), this);
    filtersDock->setObjectName(QStringLiteral("imageFiltersDock"));
    filtersDock->setWidget(filtersScroll);
    tabifyDockWidget(settingsDock, filtersDock);
    settingsDock->raise();

    statusLabel = new QLabel(this);
    statsLabel = new QLabel(this);
    statusBar()->addWidget(statusLabel, 1);
    statusBar()->addPermanentWidget(statsLabel);

    resize(1280, 760);

    capture = new calib::CaptureThread(this);
    connect(capture, &calib::CaptureThread::frameReady, this, &MainWindow::onFrameReady);
    connect(capture, &calib::CaptureThread::camerasDiscovered, this, &MainWindow::onCamerasDiscovered);
    connect(capture, &calib::CaptureThread::cameraOpened, this, &MainWindow::onCameraOpened);
    connect(capture, &calib::CaptureThread::cameraClosed, this, &MainWindow::onCameraClosed);
    connect(capture, &calib::CaptureThread::statusChanged, this, &MainWindow::onStatusChanged);
    connect(capture, &calib::CaptureThread::statisticsUpdated, this, &MainWindow::onStatisticsUpdated);

    store = std::make_unique<calib::SettingsStore>();
    profiles = store->loadCameraProfiles();
    capture->setSavedProfiles(profiles);
    connect(filters, &calib::FilterPanel::filtersChanged, capture, &calib::CaptureThread::setFilterSettings);
    filters->setSettings(store->loadFilterSettings());
    connect(filters, &calib::FilterPanel::filtersChanged, this, &MainWindow::onFiltersChanged);

    saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(500);
    connect(saveTimer, &QTimer::timeout, this, &MainWindow::saveNow);

    connect(settings, &calib::CameraSettingsWidget::modeRequested, capture, &calib::CaptureThread::selectMode);
    connect(settings, &calib::CameraSettingsWidget::flipChanged, capture, &calib::CaptureThread::setFlip);
    connect(settings, &calib::CameraSettingsWidget::controlChanged, capture, &calib::CaptureThread::setControl);
    connect(settings, &calib::CameraSettingsWidget::resetRequested, capture, &calib::CaptureThread::resetControls);
    connect(settings, &calib::CameraSettingsWidget::modeRequested, this, &MainWindow::onModeRequested);
    connect(settings, &calib::CameraSettingsWidget::flipChanged, this, &MainWindow::onFlipChanged);
    connect(settings, &calib::CameraSettingsWidget::controlChanged, this, &MainWindow::onControlRequested);
    connect(settings, &calib::CameraSettingsWidget::resetRequested, this, &MainWindow::onResetRequested);
    connect(capture, &calib::CaptureThread::cameraConfigured, this, &MainWindow::onCameraConfigured);
    connect(capture, &calib::CaptureThread::controlChanged, settings, &calib::CameraSettingsWidget::updateControl);
    capture->start();
}

MainWindow::~MainWindow(void)
{
    if(capture && capture->isRunning())
    {
        capture->stop();
    }

    Q_ASSERT(!capture || !capture->isCameraOpen());
    saveNow();
}

void MainWindow::onFrameReady(void)
{
    QImage frame = capture->takeLatestFrame();
    if(!frame.isNull())
    {
        video->setFrame(std::move(frame));
    }
}

void MainWindow::onCamerasDiscovered(const QVector<calib::CameraInfo>& cameras)
{
    this->cameras = cameras;
    cameraList->clear();
    for(const calib::CameraInfo& camera : cameras)
    {
        auto* item = new QListWidgetItem(QString::fromStdString(calib::describe(camera)), cameraList);
        item->setToolTip(cameraTooltip(camera));
        if(!camera.streamable)
        {
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
    if(cameras.isEmpty())
    {
        cameraList->addItem(tr("No cameras found"));
    }
}

void MainWindow::onCameraOpened(int listIndex, const QString& description)
{
    highlightActiveCamera(listIndex);
    statusLabel->setText(tr("Streaming: %1").arg(description));
}

void MainWindow::onCameraClosed(const QString& reason)
{
    highlightActiveCamera(-1);
    settings->clearCamera();
    saveNow();
    activeKey.clear();
    video->clearFrame();
    video->setMessage(tr("Camera closed: %1").arg(reason));
    statsLabel->clear();
}

void MainWindow::onStatusChanged(const QString& message)
{
    statusLabel->setText(message);
    video->setMessage(message);
}

void MainWindow::onStatisticsUpdated(double captureFps, quint64 droppedFrames, double filterMs)
{
    QString text = tr("%1 fps, %2 frames dropped by display").arg(captureFps, 0, 'f', 1).arg(droppedFrames);
    if(filterMs >= 0.05)
    {
        text += tr(", filters %1 ms/frame").arg(filterMs, 0, 'f', 1);
    }
    statsLabel->setText(text);
}

void MainWindow::highlightActiveCamera(int listIndex)
{
    for(int row = 0; row < cameraList->count(); ++row)
    {
        QListWidgetItem* item = cameraList->item(row);
        QFont font = item->font();
        font.setBold(row == listIndex);
        item->setFont(font);
    }
}

void MainWindow::onCameraConfigured(const calib::CameraInfo& camera, int modeIndex,
                                    const QVector<calib::CameraControlInfo>& controls,
                                    const QVector<calib::CameraControlState>& states, bool flipHorizontal,
                                    bool flipVertical)
{
    const std::string key = calib::profileKey(camera);
    if(key != activeKey)
    {
        saveNow();
    }
    activeCamera = camera;
    activeKey = key;
    settings->setCamera(camera, modeIndex, controls, states, flipHorizontal, flipVertical);
}

calib::CameraProfile* MainWindow::activeProfile(void)
{
    if(activeKey.empty())
    {
        return nullptr;
    }
    return &profiles[activeKey];
}

void MainWindow::onModeRequested(int modeIndex)
{
    calib::CameraProfile* profile = activeProfile();
    if(!profile || modeIndex < 0 || modeIndex >= static_cast<int>(activeCamera.modes.size()))
    {
        return;
    }
    profile->hasMode = true;
    profile->mode = activeCamera.modes[static_cast<size_t>(modeIndex)];
    profileChanged();
}

void MainWindow::onFlipChanged(bool horizontal, bool vertical)
{
    calib::CameraProfile* profile = activeProfile();
    if(!profile)
    {
        return;
    }
    profile->flipHorizontal = horizontal;
    profile->flipVertical = vertical;
    profileChanged();
}

void MainWindow::onControlRequested(calib::CameraControlId id, const calib::CameraControlState& state)
{
    calib::CameraProfile* profile = activeProfile();
    if(!profile)
    {
        return;
    }
    profile->controls[id] = state;
    profileChanged();
}

void MainWindow::onResetRequested(void)
{
    calib::CameraProfile* profile = activeProfile();
    if(!profile)
    {
        return;
    }
    profile->controls.clear();
    profileChanged();
}

void MainWindow::onFiltersChanged(const calib::FilterSettings&)
{
    filtersDirty = true;
    saveTimer->start();
}

void MainWindow::profileChanged(void)
{
    capture->setSavedProfile(activeKey, profiles[activeKey]);
    profileDirty = true;
    saveTimer->start();
}

void MainWindow::saveNow(void)
{
    if(!store || (!profileDirty && !filtersDirty))
    {
        return;
    }
    if(profileDirty && !activeKey.empty())
    {
        store->saveCameraProfile(activeKey, QString::fromStdString(activeCamera.name), profiles[activeKey]);
    }
    if(filtersDirty)
    {
        store->saveFilterSettings(filters->currentSettings());
    }
    profileDirty = false;
    filtersDirty = false;
    store->sync();
}
