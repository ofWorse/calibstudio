#include "capture/capturethread.h"

#include "camera/cameradevice.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QStringList>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <utility>

#ifdef _WIN32
#include <objbase.h>
#endif

Q_LOGGING_CATEGORY(lcCapture, "calibstudio.capture")

namespace calib
{

namespace
{


constexpr int kReconnectIntervalMs = 2000;
constexpr int kMaxConsecutiveReadFailures = 50;


QImage toOwnedImage(const QImage& view, bool flipX, bool flipY)
{
    return (flipX || flipY) ? view.mirrored(flipX, flipY) : view.copy();
}

QImage matToQImage(const cv::Mat& mat, bool flipX, bool flipY)
{
    if(mat.empty())
    {
        return {};
    }
    const int step = static_cast<int>(mat.step);
    switch(mat.type())
    {
    case CV_8UC3:
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        return toOwnedImage(QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_BGR888), flipX, flipY);
#else
    {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return toOwnedImage(QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888),
                            flipX, flipY);
    }
#endif
    case CV_8UC1:
        return toOwnedImage(QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_Grayscale8), flipX, flipY);
    case CV_8UC4:
        return toOwnedImage(QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_ARGB32), flipX, flipY);
    case CV_16UC1:
        return toOwnedImage(QImage(mat.data, mat.cols, mat.rows, step, QImage::Format_Grayscale16), flipX, flipY);
    default: return {};
    }
}

QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

} // namespace

CaptureThread::CaptureThread(const CameraInfo& camera, QObject* parent)
    : QThread(parent),
      camera(camera)
{
    registerCaptureTypes();
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

void CaptureThread::setFlip(bool horizontal, bool vertical)
{
    flipHorizontal = horizontal;
    flipVertical = vertical;
}

void CaptureThread::setControl(CameraControlId id, const CameraControlState& state)
{
    std::lock_guard<std::mutex> lock(commandMutex);
    pendingCommands.controls[id] = state;
    commandsPending = true;
}

void CaptureThread::resetControls(void)
{
    std::lock_guard<std::mutex> lock(commandMutex);
    pendingCommands.controls.clear();
    pendingCommands.resetControls = true;
    commandsPending = true;
}

void CaptureThread::setFilterSettings(const FilterSettings& settings)
{
    std::lock_guard<std::mutex> lock(filterMutex);
    requestedFilters = settings;
    filtersChanged = true;
}

void CaptureThread::setProfile(const CameraProfile& profile)
{
    std::lock_guard<std::mutex> lock(profileMutex);
    this->profile = profile;
}

void CaptureThread::updateCamera(const CameraInfo& camera)
{
    std::lock_guard<std::mutex> lock(cameraMutex);
    this->camera = camera;
}

CameraInfo CaptureThread::getCamera(void)
{
    std::lock_guard<std::mutex> lock(cameraMutex);
    return camera;
}

void CaptureThread::selectMode(int modeIndex)
{
    std::lock_guard<std::mutex> lock(commandMutex);
    pendingCommands.modeIndex = modeIndex;
    commandsPending = true;
}

void CaptureThread::run(void)
{
#ifdef _WIN32
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    while(!isInterruptionRequested())
    {
        CameraDevice device;
        QString error;
        if(openCamera(device, &error))
        {
            cameraOpen = true;
            droppedFrames = 0;
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                pendingCommands = PendingCommands{};
                commandsPending = false;
            }
            restoreProfile(device);
            announceCamera(device);

            const QString reason = streamUntilStopped(device);

            device.close();
            cameraOpen = false;
            qCInfo(lcCapture).noquote() << "Camera closed:" << reason;
            emit cameraClosed(reason);
            if(isInterruptionRequested())
            {
                break;
            }
            emit statusChanged(tr("Camera closed: %1. Reconnecting...").arg(reason));
        }
        else if(!isInterruptionRequested())
        {
            emit statusChanged(tr("Cannot open the camera: %1. Retrying...").arg(error));
        }
        sleepInterruptible(kReconnectIntervalMs);
    }

#ifdef _WIN32
    if(SUCCEEDED(comResult))
    {
        CoUninitialize();
    }
#endif
}

bool CaptureThread::openCamera(CameraDevice& device, QString* error)
{
    const CameraInfo current = getCamera();
    emit statusChanged(tr("Connecting to %1...").arg(qs(current.name)));

    std::string why;
    const int savedMode = savedModeIndex(current);
    bool opened = device.open(current, &why, savedMode);
    if(!opened && savedMode >= 0 && !isInterruptionRequested())
    {
        qCWarning(lcCapture).noquote() << "Saved video mode failed:" << qs(why);
        opened = device.open(current, &why);
    }
    if(!opened)
    {
        qCWarning(lcCapture).noquote() << "Cannot open" << qs(current.name) << ":" << qs(why);
        *error = qs(why);
        while(error->endsWith(QLatin1Char('.')))
        {
            error->chop(1);
        }
    }
    return opened;
}

QString CaptureThread::streamUntilStopped(CameraDevice& device)
{
    cv::Mat frame;
    int consecutiveFailures = 0;
    int framesInWindow = 0;
    std::chrono::steady_clock::duration filterTimeInWindow{};
    QElapsedTimer statsTimer;
    statsTimer.start();

    while(!isInterruptionRequested())
    {
        if(commandsPending.load())
        {
            QString failure;
            if(!applyPendingCommands(device, &failure))
            {
                return failure;
            }
        }

        if(!device.read(frame))
        {
            if(!device.isOpened())
            {
                return tr("connection lost (%1)").arg(qs(device.getLastError()));
            }
            if(++consecutiveFailures >= kMaxConsecutiveReadFailures)
            {
                return tr("the camera stopped delivering frames (unplugged?)");
            }
            msleep(10);
            continue;
        }

        consecutiveFailures = 0;

        if(filtersChanged.exchange(false))
        {
            std::lock_guard<std::mutex> lock(filterMutex);
            frameFilter.setSettings(requestedFilters);
        }
        const auto filterStart = std::chrono::steady_clock::now();
        const cv::Mat& preview = frameFilter.apply(frame);
        filterTimeInWindow += std::chrono::steady_clock::now() - filterStart;

        publishFrame(preview);

        ++framesInWindow;
        if(statsTimer.elapsed() >= 1000)
        {
            const double fps = framesInWindow * 1000.0 / static_cast<double>(statsTimer.restart());
            const double filterMs = std::chrono::duration<double, std::milli>(filterTimeInWindow).count()
                / framesInWindow;
            framesInWindow = 0;
            filterTimeInWindow = {};
            emit statisticsUpdated(fps, droppedFrames.load(), filterMs, device.getIncompleteFrames());
        }
    }

    return tr("stopped");
}

void CaptureThread::publishFrame(const cv::Mat& frame)
{
    QImage image = matToQImage(frame, flipHorizontal.load(), flipVertical.load());
    if(image.isNull())
    {
        return;
    }

    {
        QMutexLocker lock(&frameMutex);
        if(!latestFrame.isNull())
        {
            ++droppedFrames;
        }
        latestFrame = std::move(image);
    }

    if(!framePending.exchange(true))
    {
        emit frameReady();
    }
}

void CaptureThread::announceCamera(const CameraDevice& device)
{
    const QString description = QStringLiteral("%1 - %2 via %3")
                                    .arg(qs(device.getInfo().name), qs(describe(device.getActiveMode())),
                                         qs(device.backendName()));
    qCInfo(lcCapture).noquote() << "Connected:" << description;
    emit cameraOpened(description);

    QVector<CameraControlInfo> controlInfos;
    QVector<CameraControlState> controlStates;
    if(const ICameraControls* controls = device.getControls())
    {
        for(const CameraControlInfo& control : controls->list())
        {
            CameraControlState state{control.defaultValue, false};
            controls->get(control.id, &state);
            controlInfos.push_back(control);
            controlStates.push_back(state);
        }
    }
    qCInfo(lcCapture) << "Camera controls available:" << controlInfos.size();
    emit cameraConfigured(device.getInfo(), device.getActiveModeIndex(), controlInfos, controlStates,
                          flipHorizontal.load(), flipVertical.load());
}

int CaptureThread::savedModeIndex(const CameraInfo& target)
{
    std::lock_guard<std::mutex> lock(profileMutex);
    if(!profile.hasMode)
    {
        return -1;
    }
    return findMode(target.modes, profile.mode);
}

void CaptureThread::restoreProfile(CameraDevice& device)
{
    CameraProfile saved;
    {
        std::lock_guard<std::mutex> lock(profileMutex);
        saved = profile;
    }

    flipHorizontal = saved.flipHorizontal;
    flipVertical = saved.flipVertical;

    int restored = 0;
    if(ICameraControls* controls = device.getControls())
    {
        for(const CameraControlInfo& control : controls->list())
        {
            const auto value = saved.controls.find(control.id);
            if(value == saved.controls.end())
            {
                continue;
            }
            if(controls->set(control.id, value->second))
            {
                ++restored;
            }
            else
            {
                qCWarning(lcCapture) << "Could not restore" << toString(control.id);
            }
        }
    }
    if(restored > 0)
    {
        qCInfo(lcCapture).noquote()
            << QStringLiteral("Restored %1 saved controls of %2").arg(restored).arg(qs(device.getInfo().name));
    }
}

bool CaptureThread::applyPendingCommands(CameraDevice& device, QString* failure)
{
    PendingCommands commands;
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commands = std::exchange(pendingCommands, PendingCommands{});
        commandsPending = false;
    }

    const int modeCount = static_cast<int>(device.getInfo().modes.size());
    if(commands.modeIndex >= 0 && commands.modeIndex < modeCount && commands.modeIndex != device.getActiveModeIndex())
    {
        const CameraInfo opened = device.getInfo();
        emit statusChanged(
            tr("Switching %1 to %2...")
                .arg(qs(opened.name), qs(describe(opened.modes.at(static_cast<size_t>(commands.modeIndex))))));
        device.close();

        std::string error;
        if(!device.open(opened, &error, commands.modeIndex))
        {
            qCWarning(lcCapture).noquote() << "Cannot switch video mode:" << qs(error);
            if(!device.open(opened, &error))
            {
                *failure = tr("the camera could not be reopened (%1)").arg(qs(error));
                return false;
            }
        }
        restoreProfile(device);
        announceCamera(device);
    }

    ICameraControls* controls = device.getControls();
    if(!controls)
    {
        return true;
    }

    const auto applyAndReport = [this, controls](CameraControlId id, const CameraControlState& state)
    {
        if(!controls->set(id, state))
        {
            qCWarning(lcCapture) << "Camera rejected" << toString(id) << "=" << state.value
                                 << (state.automatic ? "(auto)" : "");
        }
        CameraControlState actual;
        if(controls->get(id, &actual))
        {
            emit controlChanged(id, actual);
        }
    };

    if(commands.resetControls)
    {
        for(const CameraControlInfo& control : controls->list())
        {
            applyAndReport(control.id, CameraControlState{control.defaultValue, control.supportsAuto});
        }
    }
    for(const auto& [id, state] : commands.controls)
    {
        applyAndReport(id, state);
    }
    return true;
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
