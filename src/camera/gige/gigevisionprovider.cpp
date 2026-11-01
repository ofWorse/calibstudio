#include "camera/gige/gigevisionprovider.h"

#include "camera/gige/gvcp.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QThread>
#include <QUdpSocket>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#endif

#include <algorithm>
#include <map>
#include <utility>

Q_LOGGING_CATEGORY(lcGigE, "calibstudio.gige")

namespace calib
{

struct GigEVisionProvider::Probe
{
    std::unique_ptr<QUdpSocket> socket;
    std::string interfaceName;
    std::uint32_t address = 0;
    std::uint32_t netmask = 0;
};

struct GigEVisionProvider::Reply
{
    gvcp::DeviceInfo device;
    std::size_t probe = 0;
    std::string notice;
    std::string problem;
};

namespace
{

constexpr int kForceIpAckTimeoutMs = 1000;
constexpr int kRediscoveryAttempts = 5;
constexpr int kRediscoveryDelayMs = 500;

bool inSubnet(std::uint32_t ip, std::uint32_t hostIp, std::uint32_t netmask)
{
    return netmask != 0 && (ip & netmask) == (hostIp & netmask);
}

bool addressAnswersArp(std::uint32_t ip, std::uint32_t hostIp)
{
#ifdef _WIN32
    ULONG mac[2] = {};
    ULONG length = 6;
    return SendARP(htonl(ip), htonl(hostIp), mac, &length) == NO_ERROR && length > 0;
#else
    (void)ip;
    (void)hostIp;
    return false;
#endif
}

std::string ip(std::uint32_t address)
{
    return gvcp::ipv4ToString(address);
}

} // namespace

std::uint32_t chooseCameraAddress(std::uint32_t hostIp, std::uint32_t netmask, const std::set<std::uint32_t>& used)
{
    if(netmask == 0 || netmask == 0xFFFFFFFF || netmask == 0xFFFFFFFE)
    {
        return 0;
    }
    const std::uint32_t network = hostIp & netmask;
    const std::uint32_t broadcast = network | ~netmask;
    const bool linkLocal = (hostIp & 0xFFFF0000) == 0xA9FE0000;

    std::uint32_t candidate = hostIp;
    for(int tries = 0; tries < 64; ++tries)
    {
        candidate = network | ((candidate + 1) & ~netmask);
        if(candidate == network || candidate == broadcast || candidate == hostIp || used.count(candidate))
        {
            continue;
        }
        const std::uint32_t third = (candidate >> 8) & 0xFF;
        if(linkLocal && (third == 0 || third == 255))
        {
            continue;
        }
        if(addressAnswersArp(candidate, hostIp))
        {
            continue;
        }
        return candidate;
    }
    return 0;
}

GigEVisionProvider::GigEVisionProvider(int replyTimeoutMs, bool fixAddresses)
    : replyTimeoutMs(replyTimeoutMs),
      fixAddresses(fixAddresses)
{
}

GigEVisionProvider::~GigEVisionProvider(void) = default;

std::uint16_t GigEVisionProvider::nextRequestId(void)
{
    requestId = static_cast<std::uint16_t>(requestId == 0xFFFF ? 1 : requestId + 1);
    return requestId;
}

std::vector<CameraInfo> GigEVisionProvider::enumerate(std::string* error)
{
    (void)error;
    std::vector<Probe> probes = openProbes();
    if(probes.empty())
    {
        return {};
    }
    std::vector<Reply> replies = discover(probes);

    std::map<std::string, Reply> fixed;
    if(fixAddresses)
    {
        std::set<std::uint32_t> used;
        for(const Reply& reply : replies)
        {
            used.insert(reply.device.ip);
        }
        for(const Probe& probe : probes)
        {
            used.insert(probe.address);
        }

        for(Reply& reply : replies)
        {
            Probe& probe = probes[reply.probe];
            if(inSubnet(reply.device.ip, probe.address, probe.netmask) && reply.device.ip != probe.address)
            {
                continue;
            }
            const std::uint32_t newIp = chooseCameraAddress(probe.address, probe.netmask, used);
            if(newIp == 0)
            {
                reply.problem = "no free address in the subnet of " + probe.interfaceName;
                fixed[gvcp::macToString(reply.device.mac)] = reply;
                continue;
            }
            qCInfo(lcGigE).noquote() << "Camera" << QString::fromStdString(gvcp::macToString(reply.device.mac))
                                     << "has IP" << QString::fromStdString(ip(reply.device.ip)) << "outside"
                                     << QString::fromStdString(ip(probe.address) + "/" + ip(probe.netmask))
                                     << "- assigning" << QString::fromStdString(ip(newIp));
            used.insert(newIp);
            if(forceIp(probe, reply.device, newIp, &reply.problem))
            {
                reply.notice = "IP changed from " + ip(reply.device.ip) + " to " + ip(newIp)
                    + " (temporary: the camera returns to its own setting after a restart)";
                reply.problem.clear();
            }
            fixed[gvcp::macToString(reply.device.mac)] = reply;
        }

        if(!fixed.empty())
        {
            for(int attempt = 0; attempt < kRediscoveryAttempts; ++attempt)
            {
                QThread::msleep(kRediscoveryDelayMs);
                replies = discover(probes);
                bool allReachable = true;
                for(const Reply& reply : replies)
                {
                    const Probe& probe = probes[reply.probe];
                    if(fixed.count(gvcp::macToString(reply.device.mac))
                       && !inSubnet(reply.device.ip, probe.address, probe.netmask))
                    {
                        allReachable = false;
                    }
                }
                if(allReachable)
                {
                    break;
                }
            }
        }
    }

    std::vector<CameraInfo> cameras;
    for(Reply& reply : replies)
    {
        const gvcp::DeviceInfo& device = reply.device;
        const Probe& probe = probes[reply.probe];
        const std::string mac = gvcp::macToString(device.mac);
        const auto fix = fixed.find(mac);
        if(fix != fixed.end())
        {
            reply.notice = fix->second.notice;
            reply.problem = fix->second.problem;
        }

        CameraInfo info;
        info.api = "GigE Vision";
        info.transport = CameraTransport::GigE;
        info.id = "gige:" + mac;
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
        info.details = "IP " + ip(device.ip) + ", MAC " + mac;
        if(!device.serialNumber.empty())
        {
            info.details += ", S/N " + device.serialNumber;
        }
        info.details += ", via " + probe.interfaceName;

        info.deviceIp = device.ip;
        info.hostIp = probe.address;
        info.hostNetmask = probe.netmask;
        info.streamable = inSubnet(device.ip, probe.address, probe.netmask) && device.ip != probe.address;
        if(info.streamable)
        {
            info.notice = reply.notice;
        }
        else
        {
            info.notStreamableReason = "the camera's IP " + ip(device.ip) + " is not in the subnet of "
                + probe.interfaceName + " (" + ip(probe.address) + "/" + ip(probe.netmask) + ")";
            if(!reply.problem.empty())
            {
                info.notStreamableReason += " and could not be changed: " + reply.problem;
            }
            info.notStreamableReason += ". Give the camera a persistent IP in that subnet with the vendor's tool.";
        }
        cameras.push_back(std::move(info));
    }
    return cameras;
}

std::vector<GigEVisionProvider::Probe> GigEVisionProvider::openProbes(void)
{
    std::vector<Probe> probes;
    for(const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if(!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
           || flags.testFlag(QNetworkInterface::IsLoopBack) || !flags.testFlag(QNetworkInterface::CanBroadcast))
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
            if(probe.socket->bind(entry.ip(), 0))
            {
                probes.push_back(std::move(probe));
            }
        }
    }
    return probes;
}

std::vector<GigEVisionProvider::Reply> GigEVisionProvider::discover(std::vector<Probe>& probes)
{
    const std::vector<std::uint8_t> command = gvcp::makeDiscoveryCommand(nextRequestId(), true);
    for(Probe& probe : probes)
    {
        probe.socket->writeDatagram(reinterpret_cast<const char*>(command.data()), static_cast<qint64>(command.size()),
                                    QHostAddress(QHostAddress::Broadcast), gvcp::kPort);
    }

    std::map<std::string, Reply> found;
    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < replyTimeoutMs)
    {
        for(std::size_t i = 0; i < probes.size(); ++i)
        {
            QUdpSocket& socket = *probes[i].socket;
            if(!socket.waitForReadyRead(5))
            {
                continue;
            }
            while(socket.hasPendingDatagrams())
            {
                QByteArray datagram(static_cast<int>(std::max<qint64>(socket.pendingDatagramSize(), 0)),
                                    Qt::Uninitialized);
                const qint64 received = socket.readDatagram(datagram.data(), datagram.size());
                Reply reply;
                if(received <= 0
                   || !gvcp::parseDiscoveryAck(reinterpret_cast<const std::uint8_t*>(datagram.constData()),
                                               static_cast<std::size_t>(received), &reply.device))
                {
                    continue;
                }
                reply.probe = i;
                const std::string mac = gvcp::macToString(reply.device.mac);
                const auto existing = found.find(mac);
                const bool better = existing == found.end()
                    || (!inSubnet(existing->second.device.ip, probes[existing->second.probe].address,
                                  probes[existing->second.probe].netmask)
                        && inSubnet(reply.device.ip, probes[i].address, probes[i].netmask));
                if(better)
                {
                    found[mac] = reply;
                }
            }
        }
    }

    std::vector<Reply> replies;
    for(auto& entry : found)
    {
        replies.push_back(std::move(entry.second));
    }
    return replies;
}

bool GigEVisionProvider::forceIp(Probe& probe, const gvcp::DeviceInfo& device, std::uint32_t newIp,
                                 std::string* problem)
{
    const std::uint16_t id = nextRequestId();
    const std::vector<std::uint8_t> command = gvcp::makeForceIpCommand(id, device.mac, newIp, probe.netmask, 0);
    if(probe.socket->writeDatagram(reinterpret_cast<const char*>(command.data()), static_cast<qint64>(command.size()),
                                   QHostAddress(QHostAddress::Broadcast), gvcp::kPort)
       != static_cast<qint64>(command.size()))
    {
        *problem = "cannot send FORCEIP on " + probe.interfaceName;
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < kForceIpAckTimeoutMs)
    {
        if(!probe.socket->waitForReadyRead(20))
        {
            continue;
        }
        while(probe.socket->hasPendingDatagrams())
        {
            QByteArray datagram(static_cast<int>(std::max<qint64>(probe.socket->pendingDatagramSize(), 0)),
                                Qt::Uninitialized);
            const qint64 received = probe.socket->readDatagram(datagram.data(), datagram.size());
            gvcp::AckHeader header;
            if(received <= 0
               || !gvcp::parseAckHeader(reinterpret_cast<const std::uint8_t*>(datagram.constData()),
                                        static_cast<std::size_t>(received), &header)
               || header.answer != gvcp::kForceIpAck || header.ackId != id)
            {
                continue;
            }
            if(header.status != gvcp::kStatusSuccess)
            {
                *problem = std::string("the camera refused FORCEIP: ") + gvcp::statusText(header.status);
                return false;
            }
            return true;
        }
    }

    *problem = "the camera did not acknowledge FORCEIP";
    return true;
}

} // namespace calib
