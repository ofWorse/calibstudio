#include "capture/discoverythread.h"

#include "camera/cameraprovider.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QStringList>

#include <opencv2/videoio/registry.hpp>

#ifdef _WIN32
#include <objbase.h>
#endif

Q_LOGGING_CATEGORY(lcDiscovery, "calibstudio.discovery")

namespace calib
{

namespace
{

QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

QString fingerprint(const CameraInfo& camera)
{
    return qs(camera.id + "|" + camera.name + "|" + camera.details + "|" + camera.notice + "|"
              + std::to_string(camera.streamable) + "|" + std::to_string(camera.captureIndex));
}

void logAvailableBackends(void)
{
    QStringList names;
    for(const cv::VideoCaptureAPIs api : cv::videoio_registry::getCameraBackends())
    {
        names << qs(cv::videoio_registry::getBackendName(api));
    }
    qCInfo(lcDiscovery) << "OpenCV camera backends:" << names.join(QStringLiteral(", "));
}

} // namespace

DiscoveryThread::DiscoveryThread(int intervalMs, QObject* parent)
    : QThread(parent),
      intervalMs(intervalMs)
{
    registerCaptureTypes();
}

DiscoveryThread::~DiscoveryThread(void)
{
    stop();
}

void DiscoveryThread::stop(void)
{
    if(isRunning())
    {
        requestInterruption();
        wait();
    }
}

void DiscoveryThread::refresh(void)
{
    refreshRequested = true;
}

void DiscoveryThread::run(void)
{
#ifdef _WIN32
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    logAvailableBackends();
    const auto providers = createDefaultProviders();
    QStringList lastFingerprints;
    bool first = true;

    while(!isInterruptionRequested())
    {
        if(first)
        {
            emit statusChanged(tr("Searching for cameras..."));
        }

        QVector<CameraInfo> cameras;
        for(const auto& provider : providers)
        {
            if(isInterruptionRequested())
            {
                break;
            }
            std::string error;
            for(CameraInfo& camera : provider->enumerate(&error))
            {
                cameras.push_back(std::move(camera));
            }
            if(!error.empty())
            {
                qCWarning(lcDiscovery).noquote() << provider->name() << "discovery failed:" << qs(error);
            }
        }
        if(isInterruptionRequested())
        {
            break;
        }

        QStringList fingerprints;
        for(const CameraInfo& camera : cameras)
        {
            fingerprints << fingerprint(camera);
        }
        if(first || fingerprints != lastFingerprints)
        {
            lastFingerprints = fingerprints;
            for(const CameraInfo& camera : cameras)
            {
                qCInfo(lcDiscovery).noquote() << "Found" << qs(describe(camera)) << "via" << qs(camera.api);
                if(!camera.notice.empty())
                {
                    qCInfo(lcDiscovery).noquote() << "  " << qs(camera.notice);
                    emit statusChanged(tr("%1: %2").arg(qs(camera.name), qs(camera.notice)));
                }
            }
            emit camerasDiscovered(cameras);
        }
        first = false;

        QElapsedTimer timer;
        timer.start();
        while(!isInterruptionRequested() && !refreshRequested.load() && timer.elapsed() < intervalMs)
        {
            msleep(50);
        }
        if(refreshRequested.exchange(false))
        {
            emit statusChanged(tr("Searching for cameras..."));
        }
    }

#ifdef _WIN32
    if(SUCCEEDED(comResult))
    {
        CoUninitialize();
    }
#endif
}


} // namespace calib
