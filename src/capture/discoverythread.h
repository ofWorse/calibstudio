#ifndef CALIBSTUDIO_DISCOVERYTHREAD_H
#define CALIBSTUDIO_DISCOVERYTHREAD_H

#include "capture/capturetypes.h"

#include <QString>
#include <QThread>
#include <QVector>

#include <atomic>

namespace calib
{


// Looks for cameras in the background.
class DiscoveryThread : public QThread
{
    Q_OBJECT

private:
    std::atomic<bool> refreshRequested{false};
    int intervalMs;

public:
    explicit DiscoveryThread(int intervalMs = 5000, QObject* parent = nullptr);
    ~DiscoveryThread(void) override;

    void stop(void);
    void refresh(void);

signals:
    void statusChanged(const QString& message);
    void camerasDiscovered(const QVector<calib::CameraInfo>& cameras);

protected:
    void run(void) override;
};


} // namespace calib

#endif // CALIBSTUDIO_DISCOVERYTHREAD_H
