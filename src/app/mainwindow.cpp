#include "app/mainwindow.h"

#include "capture/capturethread.h"
#include "capture/discoverythread.h"
#include "settings/settingsstore.h"
#include "ui/camerasettingswidget.h"
#include "ui/camerawindow.h"
#include "ui/filterpanel.h"
#include "ui/theme.h"
#include "ui/windowchrome.h"

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QScrollArea>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace
{


QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

QString cameraTooltip(const calib::CameraInfo& camera)
{
    QStringList lines;
    lines << qs(camera.name) << QObject::tr("Found via %1").arg(qs(camera.api));

    if(!camera.details.empty())
    {
        lines << qs(camera.details);
    }
    if(!camera.notice.empty())
    {
        lines << qs(camera.notice);
    }
    if(!camera.streamable)
    {
        lines << QObject::tr("Cannot stream: %1").arg(qs(camera.notStreamableReason));
    }

    constexpr int kMaxModesShown = 12;
    const int shown = std::min<int>(static_cast<int>(camera.modes.size()), kMaxModesShown);
    for(int i = 0; i < shown; ++i)
    {
        lines << QStringLiteral("  ") + qs(calib::describe(camera.modes[static_cast<size_t>(i)]));
    }

    if(static_cast<int>(camera.modes.size()) > shown)
    {
        lines << QObject::tr("  ... %1 more").arg(static_cast<int>(camera.modes.size()) - shown);
    }

    return lines.join(QLatin1Char('\n'));
}

QDockWidget* makeScrollDock(const QString& title, const QString& objectName, QWidget* content, QMainWindow* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    auto* dock = new QDockWidget(title, parent);
    dock->setObjectName(objectName);
    dock->setWidget(scroll);
    dock->setTitleBarWidget(new calib::DockTitleBar(dock));
    return dock;
}


} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("CalibStudio"));

    views = new QMdiArea(this);
    views->setBackground(calib::theme::kWorkArea);
    views->setOption(QMdiArea::DontMaximizeSubWindowOnActivation);
    views->installEventFilter(this);
    setCentralWidget(views);

    auto* listHost = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(8, 8, 8, 8);
    listLayout->setSpacing(6);
    auto* hint = new QLabel(tr("Tick a camera to show it in its own window."), listHost);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    cameraList = new QListWidget(listHost);
    cameraList->setSelectionMode(QAbstractItemView::NoSelection);
    cameraList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cameraList->setTextElideMode(Qt::ElideRight);
    listLayout->addWidget(hint);
    listLayout->addWidget(cameraList);
    auto* camerasDock = new QDockWidget(tr("Cameras"), this);
    camerasDock->setObjectName(QStringLiteral("camerasDock"));
    camerasDock->setWidget(listHost);
    camerasDock->setTitleBarWidget(new calib::DockTitleBar(camerasDock));
    addDockWidget(Qt::RightDockWidgetArea, camerasDock);

    settings = new calib::CameraSettingsWidget(this);
    settingsDock = makeScrollDock(tr("Camera settings"), QStringLiteral("cameraSettingsDock"), settings, this);
    addDockWidget(Qt::RightDockWidgetArea, settingsDock);
    resizeDocks({camerasDock, settingsDock}, {160, 600}, Qt::Vertical);
    resizeDocks({camerasDock}, {380}, Qt::Horizontal);

    filters = new calib::FilterPanel(this);
    filtersDock = makeScrollDock(tr("Image filters"), QStringLiteral("imageFiltersDock"), filters, this);
    tabifyDockWidget(settingsDock, filtersDock);
    settingsDock->raise();

    auto* toolBar = addToolBar(tr("View"));
    toolBar->setObjectName(QStringLiteral("viewToolBar"));
    toolBar->setMovable(false);
    toolBar->setIconSize(QSize(16, 16));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QAction* refresh = toolBar->addAction(calib::theme::icon(calib::theme::Icon::Refresh),
                                          QStringLiteral(" ") + tr("Refresh cameras"));
    QAction* tile = toolBar->addAction(calib::theme::icon(calib::theme::Icon::Tile),
                                       QStringLiteral(" ") + tr("Tile windows"));
    QAction* cascade = toolBar->addAction(calib::theme::icon(calib::theme::Icon::Cascade),
                                          QStringLiteral(" ") + tr("Cascade windows"));
    connect(tile, &QAction::triggered, this,
            [this]()
            {
                autoTile = true;
                tileWindows();
            });
    connect(cascade, &QAction::triggered, this,
            [this]()
            {
                autoTile = false;
                expandWindows();
                views->cascadeSubWindows();
            });

    statusLabel = new QLabel(this);
    statsLabel = new QLabel(this);
    statusBar()->addWidget(statusLabel, 1);
    statusBar()->addPermanentWidget(statsLabel);

    resize(1400, 820);

    store = std::make_unique<calib::SettingsStore>();
    profiles = store->loadCameraProfiles();
    QStringList enabled;
    enabledLoaded = store->loadEnabledCameras(&enabled);
    for(const QString& id : enabled)
    {
        enabledIds.insert(id.toStdString());
    }

    saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(500);
    connect(saveTimer, &QTimer::timeout, this, &MainWindow::saveNow);

    connect(cameraList, &QListWidget::itemChanged, this, &MainWindow::onCameraItemChanged);
    connect(views, &QMdiArea::subWindowActivated, this, &MainWindow::onWindowActivated);
    connect(settings, &calib::CameraSettingsWidget::modeRequested, this, &MainWindow::onModeRequested);
    connect(settings, &calib::CameraSettingsWidget::flipChanged, this, &MainWindow::onFlipChanged);
    connect(settings, &calib::CameraSettingsWidget::controlChanged, this, &MainWindow::onControlRequested);
    connect(settings, &calib::CameraSettingsWidget::resetRequested, this, &MainWindow::onResetRequested);
    connect(filters, &calib::FilterPanel::filtersChanged, this, &MainWindow::onFiltersChanged);

    discovery = new calib::DiscoveryThread(5000, this);
    connect(discovery, &calib::DiscoveryThread::camerasDiscovered, this, &MainWindow::onCamerasDiscovered);
    connect(discovery, &calib::DiscoveryThread::statusChanged, this,
            [this](const QString& message) { statusBar()->showMessage(message, 8000); });
    connect(refresh, &QAction::triggered, discovery, &calib::DiscoveryThread::refresh);

    showActiveSession();
    rebuildCameraList();
    discovery->start();
}

MainWindow::~MainWindow(void)
{
    discovery->stop();

    // Stop all cameras in parallel, then wait: each one may take a moment to
    // say goodbye to its camera.
    for(auto& entry : sessions)
    {
        entry.second->capture->requestInterruption();
    }
    for(auto& entry : sessions)
    {
        entry.second->capture->stop();
        Q_ASSERT(!entry.second->capture->isCameraOpen());
        delete entry.second->window;
        delete entry.second->capture;
    }
    sessions.clear();

    saveNow();
}

void MainWindow::onCamerasDiscovered(const QVector<calib::CameraInfo>& cameras)
{
    this->cameras = cameras;
    for(const calib::CameraInfo& camera : cameras)
    {
        if(!enabledLoaded && camera.streamable)
        {
            enabledIds.insert(camera.id);
            enabledLoaded = true;
            enabledDirty = true;
            scheduleSave();
        }

        if(CameraSession* session = findSession(camera.id))
        {
            session->capture->updateCamera(camera);
        }
        else if(camera.streamable && enabledIds.count(camera.id))
        {
            startSession(camera);
        }
    }
    rebuildCameraList();
}

void MainWindow::rebuildCameraList(void)
{
    updatingList = true;
    cameraList->clear();

    QVector<calib::CameraInfo> shown = cameras;
    for(const auto& entry : sessions)
    {
        const bool listed = std::any_of(shown.begin(), shown.end(),
                                        [&](const calib::CameraInfo& c) { return c.id == entry.first; });
        if(!listed)
        {
            shown.push_back(entry.second->camera);
        }
    }

    for(const calib::CameraInfo& camera : shown)
    {
        auto* item = new QListWidgetItem(qs(calib::describe(camera)), cameraList);
        item->setToolTip(cameraTooltip(camera));
        item->setData(Qt::UserRole, qs(camera.id));
        const bool running = findSession(camera.id) != nullptr;
        if(camera.streamable || running)
        {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(enabledIds.count(camera.id) ? Qt::Checked : Qt::Unchecked);
        }
        else
        {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
    if(shown.isEmpty())
    {
        cameraList->addItem(tr("No cameras found"));
    }

    updatingList = false;
    highlightActiveCamera();
}

void MainWindow::onCameraItemChanged(QListWidgetItem* item)
{
    if(updatingList || !(item->flags() & Qt::ItemIsUserCheckable))
    {
        return;
    }
    const std::string id = item->data(Qt::UserRole).toString().toStdString();
    const bool enabled = item->checkState() == Qt::Checked;
    QTimer::singleShot(0, this, [this, id, enabled]() { setCameraEnabled(id, enabled); });
}

void MainWindow::setCameraEnabled(const std::string& id, bool enabled)
{
    if(enabled)
    {
        enabledIds.insert(id);
        if(!findSession(id))
        {
            const auto camera = std::find_if(cameras.begin(), cameras.end(),
                                             [&](const calib::CameraInfo& c) { return c.id == id; });
            if(camera != cameras.end() && camera->streamable)
            {
                startSession(*camera);
            }
        }
    }
    else
    {
        enabledIds.erase(id);
        stopSession(id);
    }
    enabledDirty = true;
    scheduleSave();
    rebuildCameraList();
}

void MainWindow::startSession(const calib::CameraInfo& camera)
{
    auto owned = std::make_unique<CameraSession>();
    CameraSession* session = owned.get();
    session->camera = camera;
    session->profileKey = calib::profileKey(camera);
    profileNames[session->profileKey] = qs(camera.name);
    session->filters = store->loadFilterSettings(session->profileKey);

    session->capture = new calib::CaptureThread(camera, this);
    const auto profile = profiles.find(session->profileKey);
    if(profile != profiles.end())
    {
        session->capture->setProfile(profile->second);
    }
    session->capture->setFilterSettings(session->filters);

    session->window = new calib::CameraWindow(camera.id, qs(camera.name));
    sessions[camera.id] = std::move(owned);

    calib::CameraWindow* window = session->window;
    calib::CaptureThread* capture = session->capture;
    connect(window, &calib::CameraWindow::arranged, this, [this]() { autoTile = false; });
    connect(window, &calib::CameraWindow::closeRequested, this, [this](const std::string& id)
            { QTimer::singleShot(0, this, [this, id]() { setCameraEnabled(id, false); }); });
    connect(capture, &calib::CaptureThread::frameReady, window,
            [session]()
            {
                QImage frame = session->capture->takeLatestFrame();
                if(!frame.isNull())
                {
                    session->window->setFrame(std::move(frame));
                }
            });
    connect(capture, &calib::CaptureThread::statusChanged, window,
            [this, session](const QString& message)
            {
                session->status = message;
                session->window->setMessage(message);
                if(session == activeSession())
                {
                    showStatus();
                }
            });
    connect(capture, &calib::CaptureThread::cameraOpened, window,
            [this, session](const QString& description)
            {
                session->window->setLinkState(calib::CameraLinkState::Streaming);
                session->status = tr("Streaming: %1").arg(description);
                if(session == activeSession())
                {
                    showStatus();
                }
            });
    connect(capture, &calib::CaptureThread::cameraClosed, window,
            [this, session](const QString& reason)
            {
                session->configured = false;
                session->statistics.clear();
                session->window->clearFrame();
                session->window->setFrameRate(0.0);
                session->window->setLinkState(calib::CameraLinkState::Lost);
                session->window->setMessage(tr("Camera closed: %1").arg(reason));
                if(session == activeSession())
                {
                    settings->clearCamera();
                    showStatus();
                }
            });
    connect(capture, &calib::CaptureThread::statisticsUpdated, window,
            [this, session](double fps, quint64 dropped, double filterMs, quint64 incomplete)
            {
                QString text = tr("%1 fps, %2 frames dropped by display").arg(fps, 0, 'f', 1).arg(dropped);
                if(filterMs >= 0.05)
                {
                    text += tr(", filters %1 ms/frame").arg(filterMs, 0, 'f', 1);
                }
                if(incomplete > 0)
                {
                    text += tr(", %1 incomplete frames (lost network packets)").arg(incomplete);
                }
                session->statistics = text;
                session->window->setFrameRate(fps);
                if(session == activeSession())
                {
                    showStatus();
                }
            });
    connect(capture, &calib::CaptureThread::cameraConfigured, window,
            [this, session](const calib::CameraInfo& info, int modeIndex,
                            const QVector<calib::CameraControlInfo>& controls,
                            const QVector<calib::CameraControlState>& states, bool flipHorizontal, bool flipVertical)
            {
                session->camera = info;
                session->configured = true;
                session->modeIndex = modeIndex;
                session->controls = controls;
                session->states = states;
                session->flipHorizontal = flipHorizontal;
                session->flipVertical = flipVertical;
                if(session == activeSession())
                {
                    showActiveSession();
                }
            });
    connect(capture, &calib::CaptureThread::controlChanged, window,
            [this, session](calib::CameraControlId id, const calib::CameraControlState& state)
            {
                for(int i = 0; i < session->controls.size(); ++i)
                {
                    if(session->controls[i].id == id && i < session->states.size())
                    {
                        session->states[i] = state;
                    }
                }
                if(session == activeSession())
                {
                    settings->updateControl(id, state);
                }
            });

    views->addSubWindow(window);
    window->show();
    if(autoTile)
    {
        tileWindows();
    }
    views->setActiveSubWindow(window);
    activeId = camera.id;
    showActiveSession();
    capture->start();
}

void MainWindow::stopSession(const std::string& id)
{
    const auto it = sessions.find(id);
    if(it == sessions.end())
    {
        return;
    }
    CameraSession* session = it->second.get();
    session->capture->stop();
    delete session->window;
    delete session->capture;
    sessions.erase(it);

    if(!findSession(activeId))
    {
        activeId.clear();
        if(auto* window = qobject_cast<calib::CameraWindow*>(views->activeSubWindow()))
        {
            activeId = window->getCameraId();
        }
        else if(!sessions.empty())
        {
            activeId = sessions.begin()->first;
        }
        showActiveSession();
    }
    if(autoTile)
    {
        tileWindows();
    }
}

void MainWindow::tileWindows(void)
{
    expandWindows();
    views->tileSubWindows();
}

void MainWindow::expandWindows(void)
{
    for(QMdiSubWindow* window : views->subWindowList())
    {
        if(auto* cameraWindow = qobject_cast<calib::CameraWindow*>(window))
        {
            cameraWindow->setCollapsed(false);
        }
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == views && event->type() == QEvent::Resize && autoTile)
    {
        const QList<QMdiSubWindow*> windows = views->subWindowList();
        const bool anyMaximized = std::any_of(windows.begin(), windows.end(),
                                              [](const QMdiSubWindow* w) { return w->isMaximized(); });
        if(!anyMaximized)
        {
            QTimer::singleShot(0, this, &MainWindow::tileWindows);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::CameraSession* MainWindow::findSession(const std::string& id)
{
    const auto it = sessions.find(id);
    return it == sessions.end() ? nullptr : it->second.get();
}

MainWindow::CameraSession* MainWindow::activeSession(void)
{
    return activeId.empty() ? nullptr : findSession(activeId);
}

void MainWindow::highlightActiveCamera(void)
{
    updatingList = true;
    for(int row = 0; row < cameraList->count(); ++row)
    {
        QListWidgetItem* item = cameraList->item(row);
        QFont font = item->font();
        font.setBold(!activeId.empty() && item->data(Qt::UserRole).toString().toStdString() == activeId);
        item->setFont(font);
    }
    updatingList = false;
}

void MainWindow::onWindowActivated(QMdiSubWindow* window)
{
    auto* cameraWindow = qobject_cast<calib::CameraWindow*>(window);
    if(!cameraWindow || cameraWindow->getCameraId() == activeId)
    {
        return;
    }
    activeId = cameraWindow->getCameraId();
    showActiveSession();
}

void MainWindow::showActiveSession(void)
{
    CameraSession* session = activeSession();
    updatingPanels = true;
    if(session && session->configured)
    {
        settings->setCamera(session->camera, session->modeIndex, session->controls, session->states,
                            session->flipHorizontal, session->flipVertical);
    }
    else
    {
        settings->clearCamera();
    }
    filters->setSettings(session ? session->filters : store->loadFilterSettings());
    updatingPanels = false;

    settings->setEnabled(session != nullptr);
    filters->setEnabled(session != nullptr);
    const QString name = session ? qs(session->camera.name) : QString();
    settingsDock->setWindowTitle(session ? tr("Camera settings: %1").arg(name) : tr("Camera settings"));
    filtersDock->setWindowTitle(session ? tr("Image filters: %1").arg(name) : tr("Image filters"));
    showStatus();
    highlightActiveCamera();
}

void MainWindow::showStatus(void)
{
    const CameraSession* session = activeSession();
    if(!session)
    {
        statusLabel->setText(sessions.empty() ? tr("No camera is shown. Tick one in the Cameras list.") : QString());
        statsLabel->clear();
        return;
    }
    statusLabel->setText(qs(session->camera.name) + QStringLiteral(": ") + session->status);
    statsLabel->setText(session->statistics);
}

calib::CameraProfile* MainWindow::activeProfile(void)
{
    CameraSession* session = activeSession();
    return session ? &profiles[session->profileKey] : nullptr;
}

void MainWindow::onModeRequested(int modeIndex)
{
    CameraSession* session = activeSession();
    if(updatingPanels || !session)
    {
        return;
    }
    session->capture->selectMode(modeIndex);
    if(modeIndex >= 0 && modeIndex < static_cast<int>(session->camera.modes.size()))
    {
        calib::CameraProfile* profile = activeProfile();
        profile->hasMode = true;
        profile->mode = session->camera.modes[static_cast<size_t>(modeIndex)];
        profileChanged(*session);
    }
}

void MainWindow::onFlipChanged(bool horizontal, bool vertical)
{
    CameraSession* session = activeSession();
    if(updatingPanels || !session)
    {
        return;
    }
    session->capture->setFlip(horizontal, vertical);
    session->flipHorizontal = horizontal;
    session->flipVertical = vertical;
    calib::CameraProfile* profile = activeProfile();
    profile->flipHorizontal = horizontal;
    profile->flipVertical = vertical;
    profileChanged(*session);
}

void MainWindow::onControlRequested(calib::CameraControlId id, const calib::CameraControlState& state)
{
    CameraSession* session = activeSession();
    if(updatingPanels || !session)
    {
        return;
    }
    session->capture->setControl(id, state);
    activeProfile()->controls[id] = state;
    profileChanged(*session);
}

void MainWindow::onResetRequested(void)
{
    CameraSession* session = activeSession();
    if(updatingPanels || !session)
    {
        return;
    }
    session->capture->resetControls();
    activeProfile()->controls.clear();
    profileChanged(*session);
}

void MainWindow::onFiltersChanged(const calib::FilterSettings& filterSettings)
{
    CameraSession* session = activeSession();
    if(updatingPanels || !session)
    {
        return;
    }
    session->filters = filterSettings;
    session->capture->setFilterSettings(filterSettings);
    dirtyFilters[session->profileKey] = filterSettings;
    scheduleSave();
}

void MainWindow::profileChanged(CameraSession& session)
{
    const calib::CameraProfile& profile = profiles[session.profileKey];
    for(auto& entry : sessions)
    {
        if(entry.second->profileKey == session.profileKey)
        {
            entry.second->capture->setProfile(profile);
        }
    }
    dirtyProfiles.insert(session.profileKey);
    scheduleSave();
}

void MainWindow::scheduleSave(void)
{
    saveTimer->start();
}

void MainWindow::saveNow(void)
{
    if(!store || (dirtyProfiles.empty() && dirtyFilters.empty() && !enabledDirty))
    {
        return;
    }
    for(const std::string& key : dirtyProfiles)
    {
        store->saveCameraProfile(key, profileNames[key], profiles[key]);
    }
    for(const auto& [key, filterSettings] : dirtyFilters)
    {
        store->saveFilterSettings(filterSettings, key);
    }
    if(enabledDirty)
    {
        QStringList ids;
        for(const std::string& id : enabledIds)
        {
            ids << qs(id);
        }
        store->saveEnabledCameras(ids);
    }
    dirtyProfiles.clear();
    dirtyFilters.clear();
    enabledDirty = false;
    store->sync();
}
