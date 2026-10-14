#include "gigevisionprovider.h"

#include "gvcp.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QUdpSocket>

#include <map>
#include <memory>
#include <utility>

namespace calib
{

namespace
{


struct Probe
{
    std::unique_ptr<QUdpSocket> socket;
    std::string interfaceName;
    std::uint32_t address = 0;
    std::uint32_t netmask = 0;
};

CameraInfo toCameraInfo(const gvcp::DeviceInfo& device, const Probe& probe)
{
    CameraInfo info;
    info.api = "GigE Vision";
    info.transport = CameraTransport::GigE;
    info.id = "gige:" + gvcp::macToString(device.mac);

    info.name = device.manufacturer;
    if(!device.model.empty())
    {
        info.name += (info.name.empty() ? "" : " ") + device.model;
    }
    if(!device.userDefinedName.empty())
    {
        info.name += " \"" + device.userDefinedName + "\"";
    }
    if(info.name.empty())
    {
        info.name = "GigE Vision camera";
    }

    info.details = "IP " + gvcp::ipv4ToString(device.ip)
                 + ", MAC " + gvcp::macToString(device.mac);

    if(!device.serialNumber.empty())
    {
        info.details += ", S/N " + device.serialNumber;
    }
    info.details += ", via " + probe.interfaceName;

    info.streamable = false;
    info.notStreamableReason =
        "GigE Vision streaming (GVSP + GenICam) is not implemented yet";

    if(probe.netmask != 0 && (device.ip & probe.netmask) != (probe.address & probe.netmask))
    {
        info.notStreamableReason += "; note: the camera is not in the subnet of "
                                  + probe.interfaceName + " ("
                                  + gvcp::ipv4ToString(probe.address) + "/"
                                  + gvcp::ipv4ToString(probe.netmask)
                                  + "), give it a persistent IP in that subnet";
    }

    return info;
}

} // namespace

GigEVisionProvider::GigEVisionProvider(int replyTimeoutMs)
    : replyTimeoutMs(replyTimeoutMs)
{
}

std::vector<CameraInfo> GigEVisionProvider::enumerate(std::string* error)
{
    requestId = static_cast<std::uint16_t>(requestId == 0xFFFF ? 1 : requestId + 1);
    const std::vector<std::uint8_t> command = gvcp::makeDiscoveryCommand(requestId, true);

    std::vector<Probe> probes;
    for(const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if(!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)
            || !flags.testFlag(QNetworkInterface::CanBroadcast))
        {
            continue;
        }
        for(const QNetworkAddressEntry& entry : iface.addressEntries())
        {
            if(entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
            {
                continue;
            }

            Probe probe;
            probe.socket = std::make_unique<QUdpSocket>();
            probe.interfaceName = iface.humanReadableName().toStdString();
            probe.address = entry.ip().toIPv4Address();
            probe.netmask = entry.netmask().toIPv4Address();

            if(!probe.socket->bind(entry.ip(), 0))
            {
                continue;
            }

            const qint64 sent = probe.socket->writeDatagram(
                reinterpret_cast<const char*>(command.data()),
                static_cast<qint64>(command.size()),
                QHostAddress(QHostAddress::Broadcast), gvcp::kPort);

            if(sent != static_cast<qint64>(command.size()))
            {
                continue;
            }

            probes.push_back(std::move(probe));
        }
    }

    if(probes.empty())
    {
        (void)error;
        return {};
    }

    std::map<std::string, CameraInfo> found;
    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < replyTimeoutMs)
    {
        for(Probe& probe : probes)
        {
            if(!probe.socket->waitForReadyRead(5))
            {
                continue;
            }

            while(probe.socket->hasPendingDatagrams())
            {
                const qint64 size = probe.socket->pendingDatagramSize();
                QByteArray datagram(static_cast<int>(size > 0 ? size : 0), Qt::Uninitialized);
                const qint64 received = probe.socket->readDatagram(datagram.data(), datagram.size());
                if(received <= 0)
                {
                    continue;
                }

                gvcp::DeviceInfo device;
                if(!gvcp::parseDiscoveryAck(reinterpret_cast<const std::uint8_t*>(datagram.constData()),
                                             static_cast<std::size_t>(received), &device))
                {
                    continue;
                }

                CameraInfo info = toCameraInfo(device, probe);
                found.emplace(info.id, std::move(info));
            }
        }
    }

    std::vector<CameraInfo> cameras;
    cameras.reserve(found.size());
    for(auto& entry : found)
    {
        cameras.push_back(std::move(entry.second));
    }

    return cameras;
}

} // namespace calib
