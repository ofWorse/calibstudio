#include "capturethread.h"

#include "cameradevice.h"
#include "cameraprovider.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QStringList>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio/registry.hpp>

#include <utility>

#ifdef _WIN32
#include <objbase.h>
#endif

Q_LOGGING_CATEGORY(lcCapture, "calibstudio.capture")

namespace calib
{

namespace
{

constexpr int kRediscoveryIntervalMs = 2000;
constexpr int kMaxConsecutiveReadFailures = 50;


QImage matToQImage(const cv::Mat& mat)
{
    if(mat.empty()) return {};
    const int step = static_cast<int>(mat.step);
    switch (mat.type())
    {
    case CV_8UC3:
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        return QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_BGR888).copy();
#else
    {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                      QImage::Format_RGB888).copy();
    }
#endif
    case CV_8UC1:
        return QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_Grayscale8).copy();
    case CV_8UC4: // BGRA in memory == ARGB32 on little-endian machines
        return QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_ARGB32).copy();
    case CV_16UC1:
        return QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_Grayscale16).copy();
    default:
        return {};
    }
}

QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

void logAvailableBackends(void)
{
    QStringList names;
    for(const cv::VideoCaptureAPIs api : cv::videoio_registry::getCameraBackends())
    {
        names << qs(cv::videoio_registry::getBackendName(api));
    }
    qCInfo(lcCapture) << "OpenCV camera backends:" << names.join(QStringLiteral(", "));
}

} // namespace

CaptureThread::CaptureThread(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<calib::CameraInfo>("calib::CameraInfo");
    qRegisterMetaType<QVector<calib::CameraInfo>>("QVector<calib::CameraInfo>");
}

CaptureThread::~CaptureThread(void)
{
    stop();
}

void CaptureThread::stop(void)
{
    if(!isRunning())
    {
        return;
    }

    requestInterruption();

    if(!wait(3000))
    {
        qCWarning(lcCapture) << "Camera driver is slow to release the device, still waiting...";
        wait();
    }
}

QImage CaptureThread::takeLatestFrame(void)
{
    QMutexLocker lock(&frameMutex);
    framePending = false;
    return std::exchange(latestFrame, QImage());
}

void CaptureThread::run(void)
{
#ifdef _WIN32
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    logAvailableBackends();
    QStringList lastCameraIds;

    while(!isInterruptionRequested())
    {
        emit statusChanged(tr("Searching for cameras..."));
        const QVector<CameraInfo> cameras = discoverCameras();
        if(isInterruptionRequested())
        {
            break;
        }

        QStringList ids;
        for(const CameraInfo& camera : cameras)
        {
            ids << qs(camera.id);
        }
        if(ids != lastCameraIds)
        {
            lastCameraIds = ids;
            for(const CameraInfo& camera : cameras)
            {
                qCInfo(lcCapture).noquote() << "Found" << qs(describe(camera))
                                            << "via" << qs(camera.api);
            }
            emit camerasDiscovered(cameras);
        }

        CameraDevice device;
        int openedIndex = -1;
        for(int i = 0; i < cameras.size() && !isInterruptionRequested(); ++i)
        {
            const CameraInfo& camera = cameras.at(i);
            if(!camera.streamable)
            {
                continue;
            }
            emit statusChanged(tr("Connecting to %1...").arg(qs(camera.name)));
            std::string error;
            if(device.open(camera, &error))
            {
                openedIndex = i;
                break;
            }
            qCWarning(lcCapture).noquote() << "Cannot open" << qs(camera.name) << ":" << qs(error);
        }

        if(device.isOpened())
        {
            cameraOpen = true;
            droppedFrames = 0;
            const QString description = QStringLiteral("%1 - %2 via %3")
                                            .arg(qs(device.getInfo().name),
                                                 qs(describe(device.getActiveMode())),
                                                 qs(device.backendName()));
            qCInfo(lcCapture).noquote() << "Connected:" << description;
            emit cameraOpened(openedIndex, description);

            const QString reason = streamUntilStopped(device);

            device.close();
            cameraOpen = false;
            qCInfo(lcCapture).noquote() << "Camera closed:" << reason;
            emit cameraClosed(reason);
            if(isInterruptionRequested())
            {
                break;
            }
        } else
        {
            emit statusChanged(cameras.isEmpty()
                                   ? tr("No camera found. Searching again...")
                                   : tr("No camera could be opened. Searching again..."));
        }
        sleepInterruptible(kRediscoveryIntervalMs);
    }

#ifdef _WIN32
    if(SUCCEEDED(comResult))
    {
        CoUninitialize();
    }
#endif
}

QVector<CameraInfo> CaptureThread::discoverCameras()
{
    QVector<CameraInfo> cameras;
    for(const auto& provider : createDefaultProviders())
    {
        if(isInterruptionRequested())
        {
            break;
        }

        std::string error;
        std::vector<CameraInfo> found = provider->enumerate(&error);
        if(!error.empty())
        {
            qCWarning(lcCapture).noquote() << provider->name() << "discovery failed:" << qs(error);
        }
        for(CameraInfo& camera : found)
        {
            cameras.push_back(std::move(camera));
        }
    }

    return cameras;
}

QString CaptureThread::streamUntilStopped(CameraDevice& device)
{
    cv::Mat frame;
    int consecutiveFailures = 0;
    int framesInWindow = 0;
    QElapsedTimer statsTimer;
    statsTimer.start();

    while(!isInterruptionRequested())
    {
        if(!device.read(frame))
        {
            if(++consecutiveFailures >= kMaxConsecutiveReadFailures)
            {
                return tr("the camera stopped delivering frames (unplugged?)");
            }
            msleep(10);
            continue;
        }

        consecutiveFailures = 0;
        publishFrame(frame);

        ++framesInWindow;
        if(statsTimer.elapsed() >= 1000)
        {
            const double fps = framesInWindow * 1000.0 / static_cast<double>(statsTimer.restart());
            framesInWindow = 0;
            emit statisticsUpdated(fps, droppedFrames.load());
        }
    }

    return tr("the application is closing");
}

void CaptureThread::publishFrame(const cv::Mat& frame)
{
    QImage image = matToQImage(frame);
    if(image.isNull())
    {
        return;
    }

    {
        QMutexLocker lock(&frameMutex);
        if(!latestFrame.isNull())
            ++droppedFrames;
        latestFrame = std::move(image);
    }

    if(!framePending.exchange(true))
    {
        emit frameReady();
    }
}

void CaptureThread::sleepInterruptible(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while(!isInterruptionRequested() && timer.elapsed() < milliseconds)
    {
        msleep(50);
    }
}

} // namespace calib
