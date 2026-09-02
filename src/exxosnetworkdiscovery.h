/*
 * Exxos/Win7: find machines on the local network and list them under Network.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef EXXOSNETWORKDISCOVERY_H
#define EXXOSNETWORKDISCOVERY_H

#include <QHash>
#include <QObject>
#include <QString>

class QUdpSocket;

/**
 * Populates Network (remote:/) with the file servers on this subnet.
 *
 * WHY THIS EXISTS.  Dolphin's own SMB browsing finds nothing on a stock
 * Debian/MX machine, and it is not a fault of the network. Every modern NAS
 * and Windows box announces itself over WS-Discovery, and the announcement
 * carries a NAME, not an address:
 *
 *     XAddrs: http://exxos_nas:5357/aaafe1b8-7fd4-4f01-8705-f1b9eb6b084b
 *
 * Windows resolves such names over NetBIOS and LLMNR. Debian resolves neither
 * unless winbind is installed and `wins` is added to /etc/nsswitch.conf, so
 * the name fails, and the SMB worker DISCARDS every host it cannot resolve:
 *
 *     kf.kio.workers.smb: Failed to resolve any WS transport address.
 *
 * The result is an empty Network view on any machine that has not had extra
 * packages installed by hand, which is every machine this theme gets copied to.
 *
 * The reply, however, arrives in a UDP datagram FROM the host. The address is
 * therefore already known and does not need resolving at all -- discarding it
 * and then failing to look the name up again is the actual bug. This listens
 * for the same replies and keeps the sender's address.
 *
 * Entries are written as remoteview .desktop files, which is how Network is
 * assembled, so they also appear for anything else that reads remote:/.
 */
class ExxosNetworkDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit ExxosNetworkDiscovery(QObject *parent = nullptr);
    ~ExxosNetworkDiscovery() override;

    /**
     * Probe the network and update the Network entries. Returns immediately;
     * replies are collected for a few seconds and written when they stop.
     */
    void scan();

Q_SIGNALS:
    /** Emitted once the entries have been written, with the number of hosts. */
    void finished(int hostCount);

private:
    void readReplies();
    void writeEntries();

    QUdpSocket *m_socket = nullptr;
    QHash<QString, QString> m_hosts;   // address -> advertised name
};

#endif // EXXOSNETWORKDISCOVERY_H
