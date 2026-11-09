#include "capture/capturethread.h"

#include "camera/cameradevice.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QStringList>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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
constexpr int kAutoPollIntervalMs = 1000;


QImage renderForDisplay(const cv::Mat& mat, int maxWidth, int maxHeight, bool flipX, bool flipY)
{
    if(mat.empty())
    {
        return {};
    }
    cv::Mat source = mat;
    if(mat.depth() == CV_16U)
    {
        mat.convertTo(source, CV_8U, 1.0 / 256.0);
    }
    else if(mat.depth() != CV_8U)
    {
        return {};
    }

    int width = source.cols;
    int height = source.rows;
    if(maxWidth > 0 && maxHeight > 0)
    {
        const double scale = std::min(static_cast<double>(maxWidth) / width, static_cast<double>(maxHeight) / height);
        width = std::max(1, static_cast<int>(std::lround(width * scale)));
        height = std::max(1, static_cast<int>(std::lround(height * scale)));
    }

    cv::Mat scaled = source;
    const int factor = std::min(source.cols / width, source.rows / height);
    if(factor >= 2)
    {
        const cv::Mat cropped = source(cv::Rect(0, 0, source.cols - source.cols % factor,
                                                source.rows - source.rows % factor));
        cv::resize(cropped, scaled, cv::Size(cropped.cols / factor, cropped.rows / factor), 0.0, 0.0, cv::INTER_AREA);
    }
    if(width != scaled.cols || height != scaled.rows)
    {
        cv::resize(scaled, scaled, cv::Size(width, height), 0.0, 0.0, cv::INTER_LINEAR);
    }

    QImage image(width, height, QImage::Format_RGB32);
    if(image.isNull())
    {
        return {};
    }
    cv::Mat target(height, width, CV_8UC4, image.bits(), static_cast<size_t>(image.bytesPerLine()));
    switch(scaled.channels())
    {
    case 1:  cv::cvtColor(scaled, target, cv::COLOR_GRAY2BGRA); break;
    case 3:  cv::cvtColor(scaled, target, cv::COLOR_BGR2BGRA); break;
    case 4:  scaled.copyTo(target); break;
    default: return {};
    }
    if(target.data != image.bits())
    {
        return {};
    }
    if(flipX || flipY)
    {
        cv::flip(target, target, flipX && flipY ? -1 : (flipX ? 1 : 0));
    }
    return image;
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

QImage CaptureThread::takeLatestFrame(QSize* sourceSize)
{
    QMutexLocker lock(&frameMutex);
    framePending = false;
    if(sourceSize)
    {
        *sourceSize = latestSourceSize;
    }
    return std::exchange(latestFrame, QImage());
}

void CaptureThread::setDisplaySize(int width, int height, double pixelRatio)
{
    displayWidth = width;
    displayHeight = height;
    displayRatio = pixelRatio > 0.0 ? pixelRatio : 1.0;
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

void CaptureThread::setAutoTarget(int brightness)
{
    autoTarget = brightness;
    autoTargetChanged = true;
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
            autoExposure.stopAll();
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
    QElapsedTimer autoPollTimer;
    autoPollTimer.start();
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

        if(autoTargetChanged.exchange(false))
        {
            autoExposure.setTarget(autoTarget.load());
        }
        if(autoExposure.isActive())
        {
            runAutoExposure(device, frame);
        }
        if(autoPollTimer.elapsed() >= kAutoPollIntervalMs)
        {
            autoPollTimer.restart();
            reportCameraAuto(device);
        }

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
    QImage image = renderForDisplay(frame, displayWidth.load(), displayHeight.load(), flipHorizontal.load(),
                                    flipVertical.load());
    if(image.isNull())
    {
        return;
    }
    image.setDevicePixelRatio(displayRatio.load());

    {
        QMutexLocker lock(&frameMutex);
        if(!latestFrame.isNull())
        {
            ++droppedFrames;
        }
        latestFrame = std::move(image);
        latestSourceSize = QSize(frame.cols, frame.rows);
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
            state.mode = autoExposure.isActive(control.id)
                ? autoExposure.modeOf(control.id)
                : (state.automatic ? ControlMode::Auto : ControlMode::Manual);
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
    if(saved.autoTarget > 0)
    {
        autoExposure.setTarget(saved.autoTarget);
    }

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
            CameraControlState state = value->second;
            const bool software = state.mode == ControlMode::Continuous && supportsSoftwareAuto(control);
            state.automatic = state.mode == ControlMode::Auto || (state.automatic && !software);
            if(controls->set(control.id, state))
            {
                ++restored;
                if(software)
                {
                    autoExposure.start(control, state.value, ControlMode::Continuous);
                }
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

    if(commands.resetControls)
    {
        autoExposure.stopAll();
        for(const CameraControlInfo& control : controls->list())
        {
            applyControl(controls, control.id,
                         CameraControlState{control.defaultValue, control.supportsAuto,
                                            control.supportsAuto ? ControlMode::Auto : ControlMode::Manual});
        }
    }
    for(const auto& [id, state] : commands.controls)
    {
        applyControl(controls, id, state);
    }
    return true;
}

void CaptureThread::applyControl(ICameraControls* controls, CameraControlId id, const CameraControlState& state)
{
    const std::vector<CameraControlInfo> infos = controls->list();
    const auto info = std::find_if(infos.begin(), infos.end(),
                                   [id](const CameraControlInfo& control) { return control.id == id; });
    if(info == infos.end())
    {
        return;
    }

    if((state.mode == ControlMode::Once || state.mode == ControlMode::Continuous) && supportsSoftwareAuto(*info))
    {
        CameraControlState current{state.value, false};
        controls->get(id, &current);
        controls->set(id, CameraControlState{current.value, false});
        autoExposure.start(*info, current.value, state.mode);
        emit controlChanged(id, CameraControlState{current.value, false, state.mode});
        return;
    }

    autoExposure.stop(id);
    CameraControlState request = state;
    request.automatic = state.mode == ControlMode::Auto || state.automatic;
    CameraControlState before;
    if(!request.automatic && controls->get(id, &before) && before.automatic)
    {
        request.value = before.value;
    }
    if(!controls->set(id, request))
    {
        qCWarning(lcCapture) << "Camera rejected" << toString(id) << "=" << request.value
                             << (request.automatic ? "(auto)" : "");
    }
    CameraControlState actual;
    if(controls->get(id, &actual))
    {
        actual.mode = actual.automatic ? ControlMode::Auto : ControlMode::Manual;
        emit controlChanged(id, actual);
    }
}

void CaptureThread::reportCameraAuto(CameraDevice& device)
{
    // In the camera's own Auto mode exposure and gain change without us:
    // read them now and then so the panel shows the real values.
    ICameraControls* controls = device.getControls();
    if(!controls)
    {
        return;
    }
    for(const CameraControlId id : {CameraControlId::Exposure, CameraControlId::Gain})
    {
        CameraControlState state;
        if(autoExposure.isActive(id) || !controls->get(id, &state) || !state.automatic)
        {
            continue;
        }
        state.mode = ControlMode::Auto;
        const auto last = reportedAutoValues.find(id);
        if(last == reportedAutoValues.end() || last->second != state.value)
        {
            reportedAutoValues[id] = state.value;
            emit controlChanged(id, state);
        }
    }
}

void CaptureThread::runAutoExposure(CameraDevice& device, const cv::Mat& frame)
{
    ICameraControls* controls = device.getControls();
    if(!controls)
    {
        autoExposure.stopAll();
        return;
    }

    const AutoExposure::Result result = autoExposure.update(frame);
    for(const auto& [id, value] : result.changes)
    {
        controls->set(id, CameraControlState{value, false});
        if(autoExposure.isActive(id))
        {
            emit controlChanged(id, CameraControlState{value, false, autoExposure.modeOf(id)});
        }
    }
    for(const CameraControlId id : result.finished)
    {
        CameraControlState state;
        controls->get(id, &state);
        state.automatic = false;
        state.mode = ControlMode::Manual;
        emit controlChanged(id, state);
        emit autoAdjustFinished(id, state);
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
