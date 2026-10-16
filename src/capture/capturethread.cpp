#include "capture/capturethread.h"

#include "camera/cameradevice.h"
#include "camera/cameraprovider.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QStringList>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio/registry.hpp>

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


constexpr int kRediscoveryIntervalMs = 2000;
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
    case CV_8UC4: // BGRA in memory == ARGB32 on little-endian machines
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
    qRegisterMetaType<calib::CameraControlId>("calib::CameraControlId");
    qRegisterMetaType<calib::CameraControlState>("calib::CameraControlState");
    qRegisterMetaType<QVector<calib::CameraControlInfo>>("QVector<calib::CameraControlInfo>");
    qRegisterMetaType<QVector<calib::CameraControlState>>("QVector<calib::CameraControlState>");
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

void CaptureThread::setSavedProfiles(const std::map<std::string, CameraProfile>& profiles)
{
    std::lock_guard<std::mutex> lock(profileMutex);
    savedProfiles = profiles;
}

void CaptureThread::setSavedProfile(const std::string& key, const CameraProfile& profile)
{
    std::lock_guard<std::mutex> lock(profileMutex);
    savedProfiles[key] = profile;
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
                qCInfo(lcCapture).noquote() << "Found" << qs(describe(camera)) << "via" << qs(camera.api);
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
            const int savedMode = savedModeIndex(camera);
            bool opened = device.open(camera, &error, savedMode);
            if(!opened && savedMode >= 0)
            {
                qCWarning(lcCapture).noquote() << "Saved video mode failed:" << qs(error);
                opened = device.open(camera, &error);
            }
            if(opened)
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
            {
                std::lock_guard<std::mutex> lock(commandMutex);
                pendingCommands = PendingCommands{};
                commandsPending = false;
            }
            restoreProfile(device);
            announceCamera(device, openedIndex);

            const QString reason = streamUntilStopped(device, openedIndex);

            device.close();
            cameraOpen = false;
            qCInfo(lcCapture).noquote() << "Camera closed:" << reason;
            emit cameraClosed(reason);
            if(isInterruptionRequested())
            {
                break;
            }
        }
        else
        {
            emit statusChanged(cameras.isEmpty() ? tr("No camera found. Searching again...")
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

QString CaptureThread::streamUntilStopped(CameraDevice& device, int listIndex)
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
            if(!applyPendingCommands(device, listIndex, &failure))
            {
                return failure;
            }
        }

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
            emit statisticsUpdated(fps, droppedFrames.load(), filterMs);
        }
    }

    return tr("the application is closing");
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

void CaptureThread::announceCamera(const CameraDevice& device, int listIndex)
{
    const QString description = QStringLiteral("%1 - %2 via %3")
                                    .arg(qs(device.getInfo().name), qs(describe(device.getActiveMode())),
                                         qs(device.backendName()));
    qCInfo(lcCapture).noquote() << "Connected:" << description;
    emit cameraOpened(listIndex, description);

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

int CaptureThread::savedModeIndex(const CameraInfo& camera)
{
    std::lock_guard<std::mutex> lock(profileMutex);
    const auto it = savedProfiles.find(profileKey(camera));
    if(it == savedProfiles.end() || !it->second.hasMode)
    {
        return -1;
    }
    return findMode(camera.modes, it->second.mode);
}

void CaptureThread::restoreProfile(CameraDevice& device)
{
    CameraProfile profile;
    {
        std::lock_guard<std::mutex> lock(profileMutex);
        const auto it = savedProfiles.find(profileKey(device.getInfo()));
        if(it != savedProfiles.end())
        {
            profile = it->second;
        }
    }

    flipHorizontal = profile.flipHorizontal;
    flipVertical = profile.flipVertical;

    int restored = 0;
    if(ICameraControls* controls = device.getControls())
    {
        for(const CameraControlInfo& control : controls->list())
        {
            const auto saved = profile.controls.find(control.id);
            if(saved == profile.controls.end())
            {
                continue;
            }
            if(controls->set(control.id, saved->second))
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

bool CaptureThread::applyPendingCommands(CameraDevice& device, int listIndex, QString* failure)
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
        const CameraInfo camera = device.getInfo();
        emit statusChanged(
            tr("Switching %1 to %2...")
                .arg(qs(camera.name), qs(describe(camera.modes.at(static_cast<size_t>(commands.modeIndex))))));
        device.close();

        std::string error;
        if(!device.open(camera, &error, commands.modeIndex))
        {
            qCWarning(lcCapture).noquote() << "Cannot switch video mode:" << qs(error);
            if(!device.open(camera, &error))
            {
                *failure = tr("the camera could not be reopened (%1)").arg(qs(error));
                return false;
            }
        }
        restoreProfile(device);
        announceCamera(device, listIndex);
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
