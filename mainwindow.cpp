#include "mainwindow.h"

#include "camera/capturethread.h"
#include "ui/videowidget.h"

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace
{


QString cameraTooltip(const calib::CameraInfo &camera)
{
    QStringList lines;
    lines << QString::fromStdString(camera.name)
          << QObject::tr("Found via %1").arg(QString::fromStdString(camera.api));

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("CalibStudio"));

    video = new calib::VideoWidget(this);
    video->setMessage(tr("Starting..."));
    setCentralWidget(video);

    cameraList = new QListWidget(this);
    cameraList->setSelectionMode(QAbstractItemView::NoSelection);
    auto *dock = new QDockWidget(tr("Cameras"), this);
    dock->setObjectName(QStringLiteral("camerasDock"));
    dock->setWidget(cameraList);
    addDockWidget(Qt::RightDockWidgetArea, dock);

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
    capture->start();
}

MainWindow::~MainWindow(void)
{
    if(capture && capture->isRunning())
    {
        capture->stop();
    }

    Q_ASSERT(!capture || !capture->isCameraOpen());
}

void MainWindow::onFrameReady(void)
{
    QImage frame = capture->takeLatestFrame();
    if(!frame.isNull())
        video->setFrame(std::move(frame));
}

void MainWindow::onCamerasDiscovered(const QVector<calib::CameraInfo> &cameras)
{
    this->cameras = cameras;
    cameraList->clear();
    for(const calib::CameraInfo &camera : cameras)
    {
        auto *item = new QListWidgetItem(QString::fromStdString(calib::describe(camera)), cameraList);
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

void MainWindow::onCameraOpened(int listIndex, const QString &description)
{
    highlightActiveCamera(listIndex);
    statusLabel->setText(tr("Streaming: %1").arg(description));
}

void MainWindow::onCameraClosed(const QString &reason)
{
    highlightActiveCamera(-1);
    video->clearFrame();
    video->setMessage(tr("Camera closed: %1").arg(reason));
    statsLabel->clear();
}

void MainWindow::onStatusChanged(const QString &message)
{
    statusLabel->setText(message);
    video->setMessage(message);
}

void MainWindow::onStatisticsUpdated(double captureFps, quint64 droppedFrames)
{
    statsLabel->setText(tr("%1 fps, %2 frames dropped by display")
                              .arg(captureFps, 0, 'f', 1)
                              .arg(droppedFrames));
}

void MainWindow::highlightActiveCamera(int listIndex)
{
    for(int row = 0; row < cameraList->count(); ++row)
    {
        QListWidgetItem *item = cameraList->item(row);
        QFont font = item->font();
        font.setBold(row == listIndex);
        item->setFont(font);
    }
}
