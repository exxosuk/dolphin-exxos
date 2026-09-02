/*
 * Exxos/Win7: find machines on the local network and list them under Network.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "exxosnetworkdiscovery.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>

namespace
{
const char WSD_GROUP[]   = "239.255.255.250";
const quint16 WSD_PORT   = 3702;
const int LISTEN_MS      = 4000;

/* Marks the files this class owns, so a sweep can remove its own stale entries
   without ever touching one the user made by hand. The places panel taught
   this lesson the hard way: a cleanup that matched on the URL prefix deleted
   the user's own Computer bookmark. */
const char OWNED_KEY[]   = "X-Exxos-Discovered";

QString entryDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
         + QLatin1String("/remoteview");
}

/* A WS-Discovery Probe with no type filter, which is what asks every device on
   the subnet to announce itself. This is the same request Dolphin's SMB worker
   makes; only the handling of the reply differs. */
QByteArray probeMessage()
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
        " xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\""
        " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
        "<s:Header>"
        "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action>"
        "<a:MessageID>urn:uuid:%1</a:MessageID>"
        "<a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>"
        "</s:Header>"
        "<s:Body><d:Probe/></s:Body>"
        "</s:Envelope>").arg(id).toUtf8();
}

/* The advertised name, purely for the label. It is deliberately NOT used to
   address the host -- that is the whole point of this class. */
QString nameFromReply(const QByteArray &datagram)
{
    static const QRegularExpression xaddrs(
        QStringLiteral("<[^>]*XAddrs>(.*?)</[^>]*XAddrs>"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = xaddrs.match(QString::fromUtf8(datagram));
    if (!m.hasMatch()) {
        return QString();
    }
    // e.g. "http://exxos_nas:5357/aaafe1b8-..." -> "exxos_nas"
    const QString url = m.captured(1).trimmed().section(QLatin1Char(' '), 0, 0);
    QString host = url.section(QStringLiteral("//"), 1).section(QLatin1Char('/'), 0, 0);
    host = host.section(QLatin1Char(':'), 0, 0);
    return host;
}

bool isOwnAddress(const QHostAddress &addr)
{
    const auto mine = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : mine) {
        if (a.isEqual(addr)) {
            return true;
        }
    }
    return false;
}
}

ExxosNetworkDiscovery::ExxosNetworkDiscovery(QObject *parent)
    : QObject(parent)
{
}

ExxosNetworkDiscovery::~ExxosNetworkDiscovery() = default;

void ExxosNetworkDiscovery::scan()
{
    if (m_socket) {
        return;   // a sweep is already running
    }

    m_hosts.clear();
    m_socket = new QUdpSocket(this);
    // Any free port: the reply comes back to whatever we sent from.
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress)) {
        delete m_socket;
        m_socket = nullptr;
        Q_EMIT finished(0);
        return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &ExxosNetworkDiscovery::readReplies);

    /* Send once per usable interface rather than once in total. With more than
       one network present -- a wired LAN and a VPN, say -- a single send goes
       out of whichever the routing table prefers, and the NAS on the other one
       is never asked. */
    const QByteArray probe = probeMessage();
    const auto interfaces = QNetworkInterface::allInterfaces();
    bool sent = false;
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || !flags.testFlag(QNetworkInterface::CanMulticast)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        m_socket->setMulticastInterface(iface);
        if (m_socket->writeDatagram(probe, QHostAddress(QLatin1String(WSD_GROUP)), WSD_PORT) > 0) {
            sent = true;
        }
    }
    if (!sent) {
        m_socket->deleteLater();
        m_socket = nullptr;
        Q_EMIT finished(0);
        return;
    }

    QTimer::singleShot(LISTEN_MS, this, [this]() {
        writeEntries();
        if (m_socket) {
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        Q_EMIT finished(m_hosts.count());
    });
}

void ExxosNetworkDiscovery::readReplies()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        m_socket->readDatagram(datagram.data(), datagram.size(), &sender);

        if (sender.isNull() || isOwnAddress(sender)) {
            continue;   // our own probe coming back round the multicast group
        }
        const QString name = nameFromReply(datagram);
        if (name.isEmpty()) {
            continue;   // not a ProbeMatch, or an announcement with no address
        }
        // THE ADDRESS COMES FROM THE DATAGRAM, never from the advertisement.
        m_hosts.insert(sender.toString().section(QLatin1Char('%'), 0, 0), name);
    }
}

void ExxosNetworkDiscovery::writeEntries()
{
    if (m_hosts.isEmpty()) {
        /* Nothing answered. That is not proof the machines are gone -- the
           network may simply be down -- so existing entries are left alone
           rather than deleted out from under someone. */
        return;
    }

    QDir dir(entryDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return;
    }

    QSet<QString> written;
    for (auto it = m_hosts.cbegin(); it != m_hosts.cend(); ++it) {
        const QString address = it.key();
        const QString name    = it.value();
        const QString file    = dir.filePath(QStringLiteral("exxos-net-%1.desktop")
                                    .arg(QString(address).replace(QLatin1Char('.'), QLatin1Char('-'))));
        written.insert(QFileInfo(file).fileName());

        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            continue;
        }
        QTextStream out(&f);
        out << "[Desktop Entry]\n"
            << "# Written by Dolphin Exxos Edition. Safe to delete; it comes back\n"
            << "# on the next scan for as long as the machine is switched on.\n"
            << "Icon=network-server\n"
            << "Type=Link\n"
            << "Name=" << name << "\n"
            // Addressed by IP: the advertised name is frequently not resolvable
            // on a Linux box, which is the whole reason this class exists.
            << "URL=smb://" << address << "/\n"
            << OWNED_KEY << "=true\n";
    }

    // Drop our own entries for machines that did not answer this time.
    const auto stale = dir.entryList({QStringLiteral("exxos-net-*.desktop")}, QDir::Files);
    for (const QString &leftover : stale) {
        if (written.contains(leftover)) {
            continue;
        }
        QFile f(dir.filePath(leftover));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        if (f.readAll().contains(OWNED_KEY)) {   // never touch a hand-made one
            f.close();
            QFile::remove(dir.filePath(leftover));
        }
    }
}
