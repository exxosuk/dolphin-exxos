/*
 * Exxos/Win7: force removable drives to notice a change of medium.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "exxosmediarescan.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QVariantMap>

namespace
{
/* Whole devices only -- /sys/block lists no partitions, and rescanning a
   partition is meaningless. `removable` is 1 for floppies, card-reader slots,
   USB media and optical drives, and 0 for fixed disks, so the fixed disks are
   never disturbed: a rescan re-reads the partition table, which is not
   something to do to the system disk on a whim. */
bool isRemovable(const QString &name)
{
    QFile f(QStringLiteral("/sys/block/%1/removable").arg(name));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    return f.readAll().trimmed() == "1";
}
}

namespace
{
/* Size in 512-byte sectors; 0 means the drive currently reports no medium. */
bool hasMedium(const QString &name)
{
    QFile f(QStringLiteral("/sys/block/%1/size").arg(name));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;   // unknown: treat as loaded, which only means we ask less often
    }
    return f.readAll().trimmed().toLongLong() > 0;
}
}

void ExxosMediaRescan::rescanRemovable(Scope scope)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return;
    }

    const QStringList devices = QDir(QStringLiteral("/sys/block"))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : devices) {
        // Skip the pseudo-devices: loop, ram, device-mapper, zram, md.
        if (!name.startsWith(QLatin1String("sd"))
            && !name.startsWith(QLatin1String("fd")) && !name.startsWith(QLatin1String("mmcblk"))) {
            continue;
        }
        /* NEVER an optical drive.
           Rescanning one with the tray open makes the drive CLOSE the tray and
           re-read the disc, so ejecting a disc and then refreshing the view
           pulled the tray shut before the disc could be taken out -- the disc
           became physically unremovable while Dolphin was open. Optical drives
           are polled by the kernel anyway (61-optical-polling-rules sets
           /sys/block/sr0/events_poll_msecs to 2500), so they never needed this:
           it is the drives with no polling that do. */
        if (name.startsWith(QLatin1String("sr"))) {
            continue;
        }
        if (!isRemovable(name)) {
            continue;
        }
        if (scope == EmptyDrivesOnly && hasMedium(name)) {
            continue;
        }

        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.UDisks2"),
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/%1").arg(name),
            QStringLiteral("org.freedesktop.UDisks2.Block"),
            QStringLiteral("Rescan"));
        call << QVariant::fromValue(QVariantMap());

        /* Fire and forget. The point is the side effect -- udisks re-reads the
           device and emits the usual signals, which the places panel and the
           computer:/ view are already listening for. Nothing here needs the
           reply, and waiting for one would block on a drive that is spinning
           up. A device that refuses is simply skipped. */
        bus.asyncCall(call);
    }
}

/* ------------------------------------------------------------------------ */

#include <QTimer>

ExxosMediaWatch::ExxosMediaWatch(QObject *parent)
    : QObject(parent)
{
}

void ExxosMediaWatch::start()
{
    poll();               // prime: whatever is there now is not a "change"
    m_primed = true;

    auto *timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, &ExxosMediaWatch::poll);
    timer->start();
}

void ExxosMediaWatch::poll()
{
    const QStringList devices = QDir(QStringLiteral("/sys/block"))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : devices) {
        if (!name.startsWith(QLatin1String("sd"))
            && !name.startsWith(QLatin1String("fd")) && !name.startsWith(QLatin1String("mmcblk"))) {
            continue;
        }
        // Optical drives are left to the kernel's own polling, which works,
        // and must never be rescanned -- that closes the tray.
        if (!isRemovable(name)) {
            continue;
        }

        QFile f(QStringLiteral("/sys/block/%1/size").arg(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const qulonglong size = f.readAll().trimmed().toULongLong();

        const auto it = m_sizes.constFind(name);
        const bool known = (it != m_sizes.constEnd());
        if (known && *it == size) {
            continue;
        }
        m_sizes.insert(name, size);
        if (!known || !m_primed) {
            continue;                    // first sighting is not a change
        }

        /* Now, and only now, is a rescan worth doing: something really has
           changed and udisks may not have noticed. */
        QDBusConnection bus = QDBusConnection::systemBus();
        if (bus.isConnected()) {
            QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.UDisks2"),
                QStringLiteral("/org/freedesktop/UDisks2/block_devices/%1").arg(name),
                QStringLiteral("org.freedesktop.UDisks2.Block"),
                QStringLiteral("Rescan"));
            call << QVariant::fromValue(QVariantMap());
            bus.asyncCall(call);
        }
        Q_EMIT mediaChanged(name, size > 0);
    }
}

/* ------------------------------------------------------------------------ */

ExxosBusySpinner *ExxosBusySpinner::instance()
{
    static ExxosBusySpinner self;
    return &self;
}

ExxosBusySpinner::ExxosBusySpinner(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(80);          // 12.5 frames a second is plenty
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_phase = (m_phase + 1) % 12;
        Q_EMIT tick();
    });
}

void ExxosBusySpinner::setBusy(const QString &udi, bool busy)
{
    if (udi.isEmpty()) {
        return;
    }
    const bool had = anyBusy();
    if (busy) {
        m_busy.insert(udi);
    } else {
        m_busy.remove(udi);
    }
    updateTimer(had);
}

void ExxosBusySpinner::setGlobalBusy(bool busy)
{
    if (m_globalBusy == busy) {
        return;
    }
    const bool had = anyBusy();
    m_globalBusy = busy;
    updateTimer(had);
}

bool ExxosBusySpinner::anyBusy() const
{
    return m_globalBusy || !m_busy.isEmpty();
}

void ExxosBusySpinner::updateTimer(bool wasBusy)
{
    const bool now = anyBusy();
    if (now && !wasBusy) {
        m_phase = 0;
        m_timer->start();
    } else if (!now && wasBusy) {
        m_timer->stop();
    }
    Q_EMIT tick();                     // repaint immediately on the change
}

bool ExxosBusySpinner::isBusy(const QString &udi) const
{
    if (udi.isEmpty()) {
        return false;                  // not a drive: the Network shortcut, say
    }
    return m_globalBusy || m_busy.contains(udi);
}
