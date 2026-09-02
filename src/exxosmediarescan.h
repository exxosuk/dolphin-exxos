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
/** Asynchronous: returns at once, results arrive as Solid device signals. */
DOLPHIN_EXPORT void rescanRemovable();
}

#endif // EXXOSMEDIARESCAN_H
