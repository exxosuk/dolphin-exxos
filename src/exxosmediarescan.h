/*
 * Exxos/Win7: force removable drives to notice a change of medium.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef EXXOSMEDIARESCAN_H
#define EXXOSMEDIARESCAN_H

#include "dolphin_export.h"

/**
 * Ask udisks2 to re-examine every removable block device.
 *
 * WHY THIS EXISTS.  A floppy could be swapped and nothing on the desktop
 * noticed: no entry in computer:/, none in the places panel, until something
 * ELSE happened -- ejecting the CD would make the floppy appear. Both views
 * were right, because Solid and udisks genuinely did not know.
 *
 * The optical drive works because a udev rule polls it:
 *
 *     /sys/block/sr0/events_poll_msecs   2500
 *     /sys/block/sdg/events_poll_msecs   -1     (the default)
 *
 * so a disc change is picked up within a couple of seconds while a disk change
 * is not, and any activity that made udisks re-examine its devices swept the
 * floppy up as a side effect. That is the "only refreshes when the CD-ROM
 * changes" behaviour exactly.
 *
 * `org.freedesktop.UDisks2.Block.Rescan` does the same job on demand and is
 * allowed for the active session without a password -- verified on this
 * machine, it returns () rather than an authorisation error. So no udev rule,
 * no root, and nothing to install: Dolphin simply asks.
 *
 * Deliberately NOT on a timer. A rescan makes a floppy drive seek, and doing
 * that every few seconds would leave it churning; it runs when there is a
 * reason to believe the answer matters -- at start-up, on entering computer:/,
 * and when the user asks for a scan.
 */
namespace ExxosMediaRescan
{
enum Scope {
    /** Only drives reporting no medium: catches an insertion, and is quiet --
     *  a drive with nothing in it answers from its controller without seeking. */
    EmptyDrivesOnly,
    /** Every removable drive, which is what catches a REMOVAL as well. Costs a
     *  read on a drive that has a disk in it, so it is not done every few
     *  seconds; a floppy left in the drive would be working constantly. */
    AllRemovable
};

/** Asynchronous: returns at once, results arrive as Solid device signals. */
DOLPHIN_EXPORT void rescanRemovable(Scope scope = AllRemovable);
}

#include <QHash>
#include <QObject>
#include <QString>

/**
 * Watches for a disk being put in or taken out, using the kernel as the source
 * of truth.
 *
 * WHY NOT JUST LISTEN TO SOLID.  Because udisks does not always hear about it.
 * With a floppy physically in the drive:
 *
 *     /sys/block/sdg/size      1440      <- the kernel
 *     udisks Block.Size        0         <- udisks, indefinitely
 *
 * so a disk swapped while Dolphin was running was never noticed, while one
 * already in the drive at start-up was picked up -- because start-up reads
 * everything fresh. That is exactly the reported behaviour.
 *
 * /sys/block/<dev>/size is the kernel's own answer and costs nothing to read:
 * it is a few bytes of already-known state, NOT a read of the disk, so this
 * can be polled every couple of seconds without touching the drive at all.
 *
 * When a size actually changes, and only then, udisks is asked to re-examine
 * that one device -- which is the moment a rescan is justified, rather than on
 * a timer the way it was being done before.
 */
class DOLPHIN_EXPORT ExxosMediaWatch : public QObject
{
    Q_OBJECT

public:
    explicit ExxosMediaWatch(QObject *parent = nullptr);

    /** Begin watching. Reads the current sizes first, so start-up is not
     *  reported as a change. */
    void start();

Q_SIGNALS:
    /** A drive gained or lost a medium. @p device is e.g. "sdg". */
    void mediaChanged(const QString &device, bool inserted);

private:
    void poll();

    QHash<QString, qulonglong> m_sizes;
    bool m_primed = false;
};

#include <QSet>

/**
 * Which drives are being worked on, so a tile can say so.
 *
 * A single bar at the bottom of the window tells you SOMETHING is happening
 * but not what; a mark on the drive itself does. This keeps the set of busy
 * devices and ticks a phase counter while any of them are, which is all a
 * widget needs to draw a spinner.
 *
 * The timer only runs while something is busy, so an idle window costs
 * nothing at all.
 */
class DOLPHIN_EXPORT ExxosBusySpinner : public QObject
{
    Q_OBJECT

public:
    static ExxosBusySpinner *instance();

    /** @p udi is the Solid UDI, as carried in the "deviceUdi" role. */
    void setBusy(const QString &udi, bool busy);
    bool isBusy(const QString &udi) const;

    /** Advances while anything is busy; the angle of the spinner. */
    int phase() const { return m_phase; }

Q_SIGNALS:
    /** A frame has passed. Widgets showing a busy drive should repaint. */
    void tick();

private:
    explicit ExxosBusySpinner(QObject *parent = nullptr);

    QSet<QString> m_busy;
    int m_phase = 0;
    class QTimer *m_timer = nullptr;
};

#endif // EXXOSMEDIARESCAN_H
