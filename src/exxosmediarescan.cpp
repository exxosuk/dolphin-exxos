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

void ExxosMediaRescan::rescanRemovable()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return;
    }

    const QStringList devices = QDir(QStringLiteral("/sys/block"))
                                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : devices) {
        // Skip the pseudo-devices: loop, ram, device-mapper, zram, md.
        if (!name.startsWith(QLatin1String("sd")) && !name.startsWith(QLatin1String("sr"))
            && !name.startsWith(QLatin1String("fd")) && !name.startsWith(QLatin1String("mmcblk"))) {
            continue;
        }
        if (!isRemovable(name)) {
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
