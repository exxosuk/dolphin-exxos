/*
    kio_computer — a "computer:/" KIO worker for KDE Frameworks 5.

    Gives Dolphin a Windows-Explorer-style "Computer" location: every storage
    device on the machine listed as an item, with label, filesystem, capacity
    and free space.

    Why this exists
    ---------------
    KDE 4 had computer:/ (and system:/, media:/). Those workers were removed in
    KF5 — verified on this machine: `kioclient5 ls computer:/` returns
    "Unknown protocol 'computer'". Nothing that ships today lists local drives
    in a file-manager view; remote:/ lists only network services.

    Why a worker rather than patching Dolphin
    -----------------------------------------
    Dolphin renders whatever a KIO worker returns, generically. It hardcodes
    only a handful of schemes and delegates everything else. So this needs zero
    Dolphin patching and survives Dolphin updates untouched.

    Written against KIO 5.78 / Qt 5.15 (Debian 11 / MX 21):
      - base class is KIO::SlaveBase   (KIO::WorkerBase does not exist here)
      - entry point is extern "C" kdemain
      - plugin installs to  <qt5 plugins>/kf5/kio/computer.so
      - protocol declared by  computer.protocol  in <kservices5>/

    Deliberately NOT provided: a graphical capacity bar. A worker controls the
    item list and its metadata, not how Dolphin paints each row — capacity bars
    exist only in KIO's Places sidebar delegate. Free/total is supplied as text.
*/

#include <KIO/SlaveBase>
#include <KIO/Global>
#include <KLocalizedString>

#include <Solid/Device>
#include <Solid/DeviceInterface>
#include <Solid/DeviceNotifier>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>
#include <Solid/OpticalDisc>
#include <Solid/OpticalDrive>

#include <QFile>
#include <QTextStream>
#include <QSet>
#include <QHash>
#include <Solid/Block>

#include <QCoreApplication>
#include <QUrl>
#include <QString>
#include <QVector>
#include <QFile>
#include <QStringList>
#include <QDir>
#include <QRegularExpression>
#include <QEventLoop>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>

#include <sys/stat.h>
#include <sys/statvfs.h>

namespace
{

struct Drive {
    QString udi;         // Solid unique id — stable, used as the item's UDS_NAME
    QString label;       // volume label, or a sensible fallback
    QString fsType;
    QString mountPath;   // empty when not mounted
    qulonglong size = 0; // bytes, from Solid
    bool mounted = false;
    bool removable = false;
    bool optical = false;
    bool network = false;
    int  driveType = -1;  // Solid::StorageDrive::DriveType, -1 when unknown
    bool unlabelled = false;  // has media, but the volume carries no label
    QString description;      // what the DRIVE is, e.g. "Floppy Disk"
    QString node;             // kernel name, e.g. "sdg" -- only used to tell
                              // otherwise identical hardware apart
    bool noMedium   = false;  // the bay itself, with nothing in it
};

/* Human-readable size. KIO::convertSize() exists, but rolling this locally
   keeps the output format identical to what the reference screenshots show. */
QString humanSize(qulonglong bytes)
{
    static const char *unit[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; ++i; }
    return QString::asprintf(i == 0 ? "%.0f%s" : "%.1f%s", v, unit[i]);
}

/* Free space for a mounted path. statvfs is used rather than
   KIO::FileSystemFreeSpaceJob because that job is asynchronous and intended for
   client-side use; a worker is synchronous and already has the mount path. */
bool freeSpace(const QString &path, qulonglong *avail, qulonglong *total)
{
    struct statvfs st;
    if (::statvfs(QFile::encodeName(path).constData(), &st) != 0)
        return false;
    *avail = qulonglong(st.f_bavail) * st.f_frsize;   // space usable by a normal user
    *total = qulonglong(st.f_blocks) * st.f_frsize;
    return true;
}

/* Network shares are not Solid StorageAccess devices, so they must come from
   the mount table. Explorer lists them under "Network Location"; this mirrors
   that. Only real network filesystems are included. */
void appendNetworkMounts(QVector<Drive> *out)
{
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    static const QStringList netFs = {
        QStringLiteral("nfs"),  QStringLiteral("nfs4"),
        QStringLiteral("cifs"), QStringLiteral("smb3"),
        QStringLiteral("sshfs"), QStringLiteral("fuse.sshfs"),
        QStringLiteral("afs"),  QStringLiteral("ncpfs"),
    };

    while (!mounts.atEnd()) {
        const QString line = QString::fromUtf8(mounts.readLine()).trimmed();
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 3)
            continue;
        if (!netFs.contains(f.at(2)))
            continue;

        Drive d;
        // /proc/mounts octal-escapes spaces as \040
        QString path = f.at(1);
        path.replace(QStringLiteral("\\040"), QStringLiteral(" "));
        d.udi       = QStringLiteral("network:") + path;
        d.mountPath = path;
        d.mounted   = true;
        d.network   = true;
        d.fsType    = f.at(2);
        d.label     = f.at(0);            // e.g. //server/share
        out->append(d);
    }
}

/* Mount state read from the kernel, not from Solid.

   Solid::StorageAccess::isAccessible() reports a cached property kept up to
   date by udisks D-Bus signals. A KIO worker is a short-lived process that
   spends its life blocked in dispatchLoop, so those signals are not
   necessarily processed before we answer: after auto-mounting a disc the view
   still read "GAMES3 - not mounted" while the kernel had it mounted at
   /media/chris/GAMES3, and re-listing did not help because the worker kept
   giving the same stale answer.

   /proc/self/mounts cannot be stale -- it is the kernel's own table. */
static QHash<QString, QString> currentMounts()
{
    QHash<QString, QString> byDevice;      // /dev/sr0 -> /media/chris/GAMES3
    QFile f(QStringLiteral("/proc/self/mounts"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return byDevice;
    }
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 2 || !parts.at(0).startsWith("/dev/")) {
            continue;
        }
        QString target = QString::fromUtf8(parts.at(1));
        target.replace(QLatin1String("\\040"), QLatin1String(" "));   // mounts escapes spaces
        byDevice.insert(QString::fromUtf8(parts.at(0)), target);
    }
    return byDevice;
}

/* The drive a volume belongs to.

   Do NOT assume the volume's immediate parent is the StorageDrive. It is for a
   partition on a disk, but an optical disc is its own device under the drive,
   and with a disc inserted the immediate parent was not a drive -- so coverage
   was never recorded and the CD appeared TWICE: once as its volume label
   ("GAMES3") and again as the empty bay ("CD-RW Drive - not mounted"). Walk up
   until a StorageDrive is found. */
static QString ancestorDriveUdi(const Solid::Device &volume)
{
    Solid::Device p = volume.parent();
    for (int depth = 0; p.isValid() && depth < 6; ++depth) {
        if (p.is<Solid::StorageDrive>()) {
            return p.udi();
        }
        p = p.parent();
    }
    return QString();
}

/* --------------------------------------------------------------------------
   Hardware sweep.

   The StorageAccess sweep above only sees devices that expose a MOUNTABLE
   VOLUME. Hardware with no medium in it does not: an empty CD/DVD tray, an
   empty card reader, a tape drive with no cartridge. So the CD drive simply
   vanished from computer:/ whenever the tray was empty, and the same gap would
   have appeared for any other empty bay. (The USB floppy happens to advertise
   a volume even when empty, which is why it always showed and the CD did not
   -- the inconsistency that made this look like a CD-specific bug.)

   Explorer lists the DRIVE whether or not there is a medium in it, so sweep the
   StorageDrive devices too and add whatever the volume sweep missed.

   Only removable and optical bays are added this way. An internal disk with no
   filesystem is not something to offer the user, and Explorer does not list one
   either.

   When a disc IS inserted it gains a StorageAccess, the first sweep picks it up
   with its real volume label, and the entry added here is skipped as already
   covered -- so the label appears by itself with no extra work.
   -------------------------------------------------------------------------- */
/* The SLOT's kernel name, with any partition suffix removed.

   Solid::Block on a udisks DRIVE object is not the whole-disk node -- measured,
   it returns whichever block device Solid happens to associate, which for a
   card reader with a card in it is the PARTITION:

       drive Generic_STORAGE_DEVICE_..._1  ->  /dev/sde1
       drive ST2000DM008_2FR102_...        ->  /dev/sdc

   so the same slot read "(sde)" when empty and "(sde1)" with a card in it. The
   suffix exists to identify the slot, which does not change when something is
   put into it, so the partition number is stripped instead of trusting Solid
   to hand back the right device. */
QString slotName(const QString &deviceNode)
{
    const QString n = deviceNode.section(QLatin1Char('/'), -1);
    static const QRegularExpression sd(QStringLiteral("^(sd[a-z]+)[0-9]*$"));
    static const QRegularExpression mmc(QStringLiteral("^(mmcblk[0-9]+)(p[0-9]+)?$"));
    static const QRegularExpression nvme(QStringLiteral("^(nvme[0-9]+n[0-9]+)(p[0-9]+)?$"));
    for (const QRegularExpression *re : { &sd, &mmc, &nvme }) {
        const QRegularExpressionMatch m = re->match(n);
        if (m.hasMatch()) {
            return m.captured(1);
        }
    }
    return n;      // sr0 and anything unrecognised: no partitions to strip
}

void appendUnmountedHardware(QVector<Drive> *out, const QSet<QString> &coveredDrives)
{
    const auto drives = Solid::Device::listFromType(Solid::DeviceInterface::StorageDrive, QString());
    for (const Solid::Device &dev : drives) {
        const auto *drive = dev.as<Solid::StorageDrive>();
        if (!drive) {
            continue;
        }

        const bool isOptical = dev.is<Solid::OpticalDrive>();
        const bool isRemovable = drive->isRemovable() || drive->isHotpluggable();
        if (!isOptical && !isRemovable) {
            continue;   // an internal disk with no volume is not worth listing
        }

        /* Already covered if a volume we found sits under this drive.

           This MUST compare against the volumes' parent UDIs. A prefix test
           does not work: udisks2 names volumes
               /org/freedesktop/UDisks2/block_devices/sdg
           and drives
               /org/freedesktop/UDisks2/drives/Sony_USB_Floppy_Drive_...
           which share no prefix at all, so every drive looked uncovered and
           the floppy -- which does advertise a volume -- was listed twice. */
        if (coveredDrives.contains(dev.udi())) {
            continue;
        }

        Drive d;
        d.udi       = dev.udi();
        d.mounted   = false;
        d.noMedium  = true;          // this entry IS the empty bay
        d.optical   = isOptical;
        d.removable = true;
        d.driveType = int(drive->driveType());
        d.size      = 0;
        d.label     = dev.displayName();
        if (d.label.isEmpty()) {
            d.label = isOptical ? i18n("CD/DVD Drive") : i18n("Removable Drive");
        }
        d.description = d.label;   // an empty bay IS just the hardware
        if (const auto *bayBlock = dev.as<Solid::Block>()) {
            d.node = slotName(bayBlock->device());
        }
        out->append(d);
    }
}

/* Make Solid notice what has happened since the last listing.

   THE BUG THIS FIXES, measured 2026-09-02:  insert a disc, and the icon view
   went on saying "No disc / not mounted" no matter how many times it was
   reloaded -- yet closing Dolphin and opening it again showed the disc.  The
   places panel, in Dolphin's own process, was right the whole time.

   Cause: Solid's UDisks2 backend builds its device list once and then keeps it
   current from D-Bus *signals*.  A KIO worker is synchronous -- between
   commands it blocks in the socket read of its dispatch loop and never runs a
   Qt event loop -- so those signals sat unread in the D-Bus socket and Solid's
   cache stayed frozen at whatever the hardware looked like when the worker
   started.  Workers are pooled and reused, so that stale process answered
   every later listing.  Restarting Dolphin "fixed" it only because it got a
   new worker.

   Verified with scratchpad/solid-stale.cpp: a loop device attached and
   detached during the run was invisible to a process that never dispatched
   events, and appeared and vanished within one cycle once processEvents() was
   called.  Nothing else about Solid changed between the two runs.

   Draining the queue costs nothing when there is nothing queued; the timed
   processEvents() returns as soon as the queue is empty.  Mount state comes
   from /proc separately (see currentMounts()) and was always fresh -- it is
   device presence, labels and media changes that needed this. */
void refreshSolid()
{
    for (int i = 0; i < 6; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
}

/* Screenshot mode: replace real volume labels with harmless ones.

   Set EXXOS_DEMO_LABELS=1. The hardware names -- "WD Blue SN570 1TB", "Sony
   USB Floppy Drive" -- are left alone, because they are what the tile is
   demonstrating and they say nothing about anybody. Only the volume labels
   change, since those are the bit that tends to carry a name, a project or a
   client on a real machine.

   Deterministic by position, so a screenshot can be retaken and look the same.
   Off unless the variable is set, so it can never affect ordinary use. */
void applyDemoLabels(QVector<Drive> *drives)
{
    static const char *fixed[]     = { "Windows", "Data", "Programs", "Music",
                                       "Photos", "Archive", "Scratch" };
    static const char *removables[] = { "USB Drive", "Camera Card",
                                        "Install Media", "Spare" };
    int f = 0, r = 0;
    for (Drive &d : *drives) {
        if (d.label.isEmpty() || d.unlabelled || d.noMedium) {
            continue;
        }
        if (d.network) {
            d.label = QStringLiteral("NAS");     // a machine name is a name
        } else if (d.optical) {
            d.label = QStringLiteral("Backup Disc");
        } else if (d.removable) {
            d.label = QString::fromLatin1(removables[r++ % 4]);
        } else {
            d.label = QString::fromLatin1(fixed[f++ % 7]);
        }
    }
}

QVector<Drive> enumerateDrives()
{
    refreshSolid();
    QVector<Drive> out;
    QSet<QString> coveredDrives;   // parent UDIs of every volume we list
    const QHash<QString, QString> mounts = currentMounts();
    const auto devices = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess, QString());

    for (const Solid::Device &dev : devices) {
        const auto *access = dev.as<Solid::StorageAccess>();
        if (!access)
            continue;

        // Ignore loop devices and other pseudo-volumes with no real size.
        const auto *volume = dev.as<Solid::StorageVolume>();
        if (volume && volume->isIgnored())
            continue;

        Drive d;
        d.udi       = dev.udi();
        /* Trust the kernel's mount table over Solid's cached flag; see
           currentMounts(). Fall back to Solid if the device node is unknown. */
        const auto *block = dev.as<Solid::Block>();
        const QString node = block ? block->device() : QString();
        if (!node.isEmpty() && mounts.contains(node)) {
            d.mounted   = true;
            d.mountPath = mounts.value(node);
        } else {
            d.mounted   = access->isAccessible();
            d.mountPath = d.mounted ? access->filePath() : QString();
        }
        d.fsType    = volume ? volume->fsType() : QString();
        d.size      = volume ? volume->size() : 0;
        d.optical   = dev.is<Solid::OpticalDisc>();

        Solid::Device parent = dev.parent();
        if (const auto *drive = parent.as<Solid::StorageDrive>()) {
            d.removable = drive->isRemovable() || drive->isHotpluggable();
            d.driveType = int(drive->driveType());
        }
        // Record coverage against the real drive, however deep it sits.
        const QString driveUdi = ancestorDriveUdi(dev);
        if (!driveUdi.isEmpty()) {
            coveredDrives.insert(driveUdi);
        }

        /* What the hardware IS, kept separate from what is IN it. The tile
           puts the description on the first line and the medium on the second:
           "Floppy Disk [no label] (vfat) - not mounted" on one line is longer
           than any tile can show, and was simply cut off. */
        {
            const Solid::Device drv(driveUdi);
            /* The MODEL, not Solid's generated display name. displayName()
               returns things like "931.5 GiB Internal Drive (nvme0n1p1)" --
               the same words for every disk in the machine, and a partition
               node the user never uses. vendor + product gives
               "Samsung SSD 870 QVO 1TB", which actually identifies the drive. */
            if (drv.isValid()) {
                d.description = (drv.vendor() + QLatin1Char(' ') + drv.product())
                                    .simplified();
                if (d.description.isEmpty()) {
                    d.description = drv.displayName();
                }
            }
        }
        if (d.description.isEmpty()) {
            d.description = dev.displayName();
        }
        d.node = slotName(node);

        d.label = volume ? volume->label() : QString();
        /* A disk can be perfectly readable and simply have no volume label --
           most floppies do not carry one. Falling back to the device
           description alone left no hint of why the name looked like hardware
           rather than media, so say so explicitly. */
        if (d.label.isEmpty() && volume) {
            d.unlabelled = true;
        }
        if (d.label.isEmpty())
            d.label = dev.displayName();                  // e.g. "14.8 GiB Removable Media"
        if (d.label.isEmpty() && !d.mountPath.isEmpty())
            d.label = d.mountPath;
        if (d.label.isEmpty())
            d.label = i18n("Storage device");

        out.append(d);
    }

    appendUnmountedHardware(&out, coveredDrives);
    appendNetworkMounts(&out);

    if (qEnvironmentVariableIsSet("EXXOS_DEMO_LABELS")) {
        applyDemoLabels(&out);
    }

    /* Tell identical hardware apart, and ONLY identical hardware.

       A multi-slot card reader presents every slot with the same model name,
       so three tiles read "Generic STORAGE DEVICE" with no way to tell which
       is which -- and no stable order either, because the view sorts by name
       and identical names tie, so tiles could swap places between refreshes.
       That is why a spinner appeared to be on one drive while a different one
       changed its text.

       The kernel name is added only where a description is shared, so the one
       floppy drive stays "Sony USB Floppy Drive" rather than gaining a "(sdg)"
       that tells the user nothing. */
    QHash<QString, int> seen;
    for (const Drive &d : out) {
        if (!d.description.isEmpty()) {
            ++seen[d.description];
        }
    }
    for (Drive &d : out) {
        if (!d.node.isEmpty() && seen.value(d.description) > 1) {
            d.description += QStringLiteral(" (%1)").arg(d.node);
        }
    }
    return out;
}

/* A shortcut into Dolphin's own Network view (remote:/), shown under
   "Network Locations" so computer:/ has the same Network node Explorer does.

   Why a shortcut rather than enumerating shares here: browsing SMB requires
   credentials per host - some hosts refuse anonymous listing - and a network
   browse can block for
   many seconds. listDir() is synchronous, so doing that here would stall the
   whole computer:/ view on every open. remote:/ already handles browsing,
   authentication and caching properly, so this defers to it.

   Shares that are actually MOUNTED still appear as real drives via
   appendNetworkMounts(), which is the true equivalent of a Windows mapped drive. */
/* Network machines that discovery has found, listed as first-class entries
   under "Network Locations" rather than hidden behind the Network shortcut.

   Explorer lists the machines themselves under Network, and that is what makes
   a NAS findable. The list comes from the same remoteview .desktop files that
   Network (remote:/) is assembled from, which is where Dolphin Exxos writes
   what it discovers (see src/exxosnetworkdiscovery.cpp) and where a hand-made
   network folder goes too. So this costs no network traffic at all: the
   discovery has already happened in Dolphin, which has an event loop, and the
   worker only reads the result.

   Protocol roots -- "smb://", "mtp:/", "bluetooth:/" -- are skipped. They are
   places to go browsing, not machines, and they are already reachable through
   the Network shortcut. Only entries naming a host are listed. */
struct NetworkPlace {
    QString id;      // desktop file base name, used as UDS_NAME
    QString label;
    QString url;
    QString icon;
};

QVector<NetworkPlace> networkPlaces()
{
    QVector<NetworkPlace> out;
    QSet<QString> seenUrls;

    const QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("remoteview"),
        QStandardPaths::LocateDirectory);

    for (const QString &dirPath : dirs) {
        QDir dir(dirPath);
        const auto files = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QString &fileName : files) {
            QFile f(dir.filePath(fileName));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            NetworkPlace p;
            p.id = QFileInfo(fileName).completeBaseName();
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                // Plain "Name=", never "Name[de]=" -- the worker has no locale
                // of its own and a translated line would be picked at random.
                if (line.startsWith(QLatin1String("Name="))) {
                    p.label = line.mid(5);
                } else if (line.startsWith(QLatin1String("URL="))) {
                    p.url = line.mid(4);
                } else if (line.startsWith(QLatin1String("Icon="))) {
                    p.icon = line.mid(5);
                }
            }
            if (p.url.isEmpty() || p.label.isEmpty()) {
                continue;
            }
            const QUrl u(p.url);
            if (u.host().isEmpty()) {
                continue;   // a protocol root, not a machine
            }
            if (seenUrls.contains(p.url)) {
                continue;   // the user's data dir shadowing the system one
            }
            seenUrls.insert(p.url);
            if (p.icon.isEmpty()) {
                p.icon = QStringLiteral("network-server");
            }
            if (qEnvironmentVariableIsSet("EXXOS_DEMO_LABELS")) {
                // A machine name is a name; see applyDemoLabels().
                p.label = QStringLiteral("NAS");
            }
            out.append(p);
        }
    }
    return out;
}

KIO::UDSEntry networkPlaceEntry(const NetworkPlace &p, int networkCount = -1)
{
    static const QString z = QStringLiteral("\u00A0");
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, p.id);
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, p.label);
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0555);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, p.icon);
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_TYPE,
                 z + i18n("Network Locations")
                   + (networkCount >= 0 ? QStringLiteral(" (%1)").arg(networkCount) : QString()));
    e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, p.url);
    return e;
}

KIO::UDSEntry networkShortcut(int networkCount = -1)
{
    /* Must use the SAME prefix scheme as categoryFor() or this item lands in a
       separate group. It previously kept its own U+200B copy, which only
       sorted last by coincidence once categoryFor() moved to U+00A0. */
    static const QString z = QStringLiteral("\u00A0");
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("network-browse"));
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Network"));
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0555);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("network-workgroup"));
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_TYPE,
                 z + i18n("Network Locations")
                   + (networkCount >= 0 ? QStringLiteral(" (%1)").arg(networkCount) : QString()));
    e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, QStringLiteral("remote:/"));
    return e;
}

/* Icon per device kind.  Previously EVERY removable device fell through to
   "drive-removable-media-usb", which is why the floppy drive was drawn as a USB
   stick.  Solid::StorageDrive::driveType() distinguishes them, so the floppy,
   card readers and optical drives now get their own icons.

   All names below were verified present in the installed icon themes, so none
   of them fall back to a generic placeholder:
     media-floppy, drive-optical, media-flash, drive-removable-media-usb,
     drive-harddisk  -> /usr/share/icons/breeze/devices/
   No Windows artwork is needed for any of these. */
QString iconFor(const Drive &d)
{
    if (d.network)   return QStringLiteral("folder-network");
    if (d.optical)   return QStringLiteral("drive-optical");

    switch (d.driveType) {
    case 1:  return QStringLiteral("drive-optical");   // CdromDrive
    /* drive-floppy-35, not media-floppy: this is the DRIVE, not a disk, and
       the two are drawn very differently in this icon theme. Measured at a
       nominal 48px, media-floppy's artwork fills the whole 48x48 canvas while
       drive-harddisk only occupies a 48x25 band through the middle, so the
       floppy came out looking roughly twice the size of every drive beside it.
       drive-floppy-35 is 48x36 and sits far closer to the others. */
    case 2:  return QStringLiteral("drive-floppy-35"); // Floppy
    case 3:  return QStringLiteral("media-tape");      // Tape
    case 4:                                            // CompactFlash
    case 5:                                            // MemoryStick
    case 6:                                            // SmartMedia
    case 7:                                            // SdMmc
    case 8:  return QStringLiteral("media-flash");     // Xd
    default: break;
    }

    if (d.removable) return QStringLiteral("drive-removable-media-usb");
    return QStringLiteral("drive-harddisk");
}

/* A usage bar drawn with Unicode block characters.

   Dolphin CANNOT draw a real graphical capacity bar for a worker's items:
   verified on this machine that libdolphinprivate.so contains no
   capacity/freespace/progress drawing at all, while KCapacityBar::drawCapacityBar
   exists only in libKF5KIOFileWidgets - i.e. the Places sidebar. A worker
   supplies item metadata, not painting. Block characters are the closest thing
   that works without patching Dolphin. */
/* Cell count matched to the reference: the Windows 7 bar in computer.PNG
   measures 188 x 13 px.  At this view's text metrics a block glyph is about
   7 px wide, so ~26 glyphs reproduce that length; 24 cells plus the two edge
   caps lands there.  (Was 10, which read as a stubby marker rather than a bar.) */
QString usageBar(qulonglong used, qulonglong total, int cells = 24)
{
    if (total == 0)
        return QString();
    int filled = int((double(used) / double(total)) * cells + 0.5);
    filled = qBound(0, filled, cells);
    QString bar;
    bar.reserve(cells + 2);
    bar += QChar(0x2595);                                  // ▕ left edge
    for (int i = 0; i < cells; ++i)
        bar += QChar(i < filled ? 0x2588 : 0x2591);        // █ full / ░ light
    bar += QChar(0x258F);                                  // ▏ right edge
    return bar;
}

/* Explorer-style category, surfaced as UDS_DISPLAY_TYPE. With Dolphin's
   "Show in Groups" enabled and sorting by Type, these become the group
   headings - the same split Windows Explorer uses. */
QString categoryFor(const Drive &d, int count)
{
    /* Dolphin sorts group headings lexicographically by this string, which
       would give  Devices..., Hard..., Network...  - not Explorer's order of
       Hard Disk Drives, Devices with Removable Storage, Network Locations.

       An earlier version prefixed each heading with N ZERO WIDTH SPACEs
       (U+200B).  That does NOT work and was verified failing on screen: the
       groups still came out Devices, Hard, Network.  U+200B is a format
       character (category Cf) and a Default_Ignorable_Code_Point, so Qt's
       collator discards it entirely and sorts on the visible text alone.

       NO-BREAK SPACE (U+00A0) is used instead.  It is a space separator
       (category Zs), which the collator does weigh, and it sorts before every
       letter.  Comparing position by position, a LONGER run of leading spaces
       therefore sorts EARLIER, so the counts are the reverse of the old
       scheme:
         3 x NBSP  ->  Hard Disk Drives                (sorts 1st)
         2 x NBSP  ->  Devices with Removable Storage  (sorts 2nd)
         1 x NBSP  ->  Network Locations               (sorts 3rd)
       Cost: the headings carry a small leading indent.  Explorer indents its
       headings too, so this reads as intentional. */
    /* Explorer also puts the item count in the heading -- "Hard Disk Drives (7)".
       count < 0 means "unknown" (the stat() path handles a single item and has
       no listing to count), in which case the suffix is omitted. */
    static const QString z = QStringLiteral("\u00A0");
    const QString n = (count >= 0) ? QStringLiteral(" (%1)").arg(count) : QString();

    if (d.network)                return z + i18n("Network Locations") + n;
    if (d.optical || d.removable) return z + z + i18n("Devices with Removable Storage") + n;
    return z + z + z + i18n("Hard Disk Drives") + n;
}

/* Which heading a drive belongs to, as a plain key for counting. */
int categoryKey(const Drive &d)
{
    if (d.network)                return 2;
    if (d.optical || d.removable) return 1;
    return 0;
}

/* The user-visible line, e.g.
     "System (ntfs) — 520.8GB free of 931.6GB"
     "Data (ntfs) — not mounted"                                        */
QString describe(const Drive &d)
{
    /* Two halves, separated by an em dash: what the HARDWARE is, and what is
       IN it. The tile draws them as two lines (see KStandardItemListWidget),
       because one line clipped whichever half came second -- and that was
       usually the label, the half that says which disk this is.

           "Generic STORAGE DEVICE \u2014 [no label]"
           "Samsung SSD 870 QVO 1TB \u2014 Backup (ntfs)"
           "CD-RW/DVD\u00B1RW DL Drive \u2014 No disc"
           "External Floppy Drive \u2014 [no label], not mounted"

       The filesystem type is kept for fixed disks, where it is worth knowing
       and there is room, and dropped for removable media, where the line is
       needed for the label itself. */
    const bool removableKind = d.removable || d.optical;

    QString medium;
    if (d.noMedium) {
        medium = d.optical ? i18n("No disc") : i18n("Empty");
    } else {
        medium = d.unlabelled ? i18n("[no label]") : d.label;
        if (!removableKind && !d.fsType.isEmpty()) {
            medium += QStringLiteral(" (%1)").arg(d.fsType);
        }
        if (!d.mounted) {
            medium += QStringLiteral(", ") + i18n("not mounted");
        }
    }

    /* No description, or one that just repeats the label, would give two
       identical lines. Fall back to the single-line form. */
    const QString hardware = d.description;
    if (hardware.isEmpty() || hardware == d.label) {
        if (!d.mounted || d.noMedium) {
            return d.label + QStringLiteral(" \u2014 ") + medium;
        }
        return d.label;
    }

    /* WHICH half goes first depends on whether the medium has a name.

       When it does, that name is what the user is looking for -- so it
       leads, in bold, with the drive model underneath as
       supporting detail. When it does not, the only identity available is the
       hardware, so that leads instead and the second line says what the state
       is: "[no label]", "No disc", "Empty". Putting "Empty" in bold above the
       drive it belongs to would be exactly backwards. */
    const bool mediumHasName = !d.noMedium && !d.unlabelled && !d.label.isEmpty();
    if (mediumHasName) {
        QString lead = d.label;
        if (!removableKind && !d.fsType.isEmpty()) {
            lead += QStringLiteral(" (%1)").arg(d.fsType);
        }
        QString under = hardware;
        if (!d.mounted) {
            under += QStringLiteral(", ") + i18n("not mounted");
        }
        return lead + QStringLiteral(" \u2014 ") + under;
    }

    return hardware + QStringLiteral(" \u2014 ") + medium;
}

class ComputerProtocol : public KIO::SlaveBase
{
public:
    ComputerProtocol(const QByteArray &pool, const QByteArray &app)
        : KIO::SlaveBase("computer", pool, app) {}

    void listDir(const QUrl &url) override;
    void stat(const QUrl &url) override;

private:
    static KIO::UDSEntry rootEntry();
    static KIO::UDSEntry driveEntry(const Drive &d, int categoryCount = -1);
};

KIO::UDSEntry ComputerProtocol::rootEntry()
{
    KIO::UDSEntry e;
    e.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Computer"));
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0555);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("computer"));
    return e;
}

KIO::UDSEntry ComputerProtocol::driveEntry(const Drive &d, int categoryCount)
{
    KIO::UDSEntry e;

    /* UDS_NAME is the internal name and must not contain '/'. The Solid UDI is
       stable across reboots and unique, so it is used as the identity, while
       UDS_DISPLAY_NAME carries what the user actually reads. */
    QString name = d.udi;
    name.replace(QLatin1Char('/'), QLatin1Char('_'));

    e.fastInsert(KIO::UDSEntry::UDS_NAME, name);
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, describe(d));
    e.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    e.fastInsert(KIO::UDSEntry::UDS_ACCESS, 0555);
    e.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    e.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, iconFor(d));
    e.fastInsert(KIO::UDSEntry::UDS_DISPLAY_TYPE, categoryFor(d, categoryCount));

    /* Capacity, for the patched Dolphin's tile view.  UDS_EXTRA and
       UDS_EXTRA+1 carry free and total bytes as decimal strings;
       KFileItemModel::retrieveData() turns them into the "freeSpace" /
       "totalSpace" roles and KStandardItemListWidget draws a real graphical
       bar from them.  An unpatched Dolphin simply ignores these fields. */
    /* Exxos: what the patched Dolphin needs to offer Mount / Unmount / Eject
       on a device in the icon view (see DolphinContextMenu).  The item's
       UDS_NAME is the UDI with '/' replaced by '_', which cannot be reversed
       -- real UDIs contain underscores ("block_devices") -- so the UDI is
       carried verbatim instead of being reconstructed. */
    e.fastInsert(KIO::UDSEntry::UDS_EXTRA + 2, d.udi);
    e.fastInsert(KIO::UDSEntry::UDS_EXTRA + 3,
                 d.mounted  ? QStringLiteral("mounted")
                 : d.noMedium ? QStringLiteral("nomedium")
                              : QStringLiteral("unmounted"));
    e.fastInsert(KIO::UDSEntry::UDS_EXTRA + 4,
                 d.optical ? QStringLiteral("optical")
                 : d.removable ? QStringLiteral("removable")
                               : QStringLiteral("fixed"));

    if (d.mounted) {
        qulonglong availB = 0, totalB = 0;
        if (freeSpace(d.mountPath, &availB, &totalB) && totalB > 0) {
            e.fastInsert(KIO::UDSEntry::UDS_EXTRA,     QString::number(availB));
            e.fastInsert(KIO::UDSEntry::UDS_EXTRA + 1, QString::number(totalB));
        }
    }

    if (d.mounted) {
        /* UDS_TARGET_URL makes the item a link to the real location, so opening
           it leaves computer:/ and enters the drive itself.

           UDS_LOCAL_PATH is deliberately NOT set. Setting it tells KIO the item
           *is* a local directory, whereupon Dolphin generates a folder-content
           preview that overrides UDS_ICON_NAME - mounted drives rendered as
           yellow folders showing thumbnails of their contents instead of drive
           icons (observed 2026-08-31). TARGET_URL alone is sufficient for
           click-through navigation. */
        const QUrl target = QUrl::fromLocalFile(d.mountPath);
        e.fastInsert(KIO::UDSEntry::UDS_TARGET_URL, target.toString());

        /* UDS_URL is deliberately NOT set, for the same reason UDS_LOCAL_PATH
           is not: it makes the item look like a plain directory, and Dolphin
           then draws a folder-contents preview over the drive icon. Search
           results are kept out of computer:/ by resolving the search root in
           the search box instead, which costs the tile view nothing. */
        qulonglong avail = 0, total = 0;
        if (freeSpace(d.mountPath, &avail, &total))
            e.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<long long>(total));
    } else if (d.size > 0) {
        e.fastInsert(KIO::UDSEntry::UDS_SIZE, static_cast<long long>(d.size));
    }

    /* A drive that could be mounted and is not gets a marker on its icon, so
       "not mounted" is visible at a glance rather than only in the text under
       it. Explorer has nothing equivalent because Windows mounts everything;
       here it is the difference between a drive you can open and one you
       cannot. */
    if (d.mountPath.isEmpty() && !d.noMedium && !d.network && d.size > 0) {
        e.fastInsert(KIO::UDSEntry::UDS_ICON_OVERLAY_NAMES,
                     QStringLiteral("emblem-unmounted"));
    }

    return e;
}

/* Mount a drive that is not currently mounted, and return its mount path.

   WHY: opening an unmounted drive in computer:/ used to fail outright -- the
   worker answered ERR_DOES_NOT_EXIST for any path below the root, so a USB
   floppy with a disk in it errored when clicked even though the disk was
   perfectly readable.  Windows mounts on open; this reproduces that.

   Solid::StorageAccess::setup() is ASYNCHRONOUS while a KIO worker is
   synchronous, so the result is waited for on a local event loop.  The timeout
   is deliberately generous: a USB floppy can take several seconds to spin up
   and read its boot sector. */
static bool mountDrive(const QString &udi, QString *mountPath, QString *errMsg)
{
    Solid::Device dev(udi);
    auto *access = dev.as<Solid::StorageAccess>();
    if (!access) {
        *errMsg = i18n("This device cannot be mounted.");
        return false;
    }
    if (access->isAccessible()) {              // already mounted - nothing to do
        *mountPath = access->filePath();
        return true;
    }

    QEventLoop loop;
    bool ok = false;
    QString failure;

    QObject::connect(access, &Solid::StorageAccess::setupDone,
                     [&](Solid::ErrorType err, QVariant data, const QString &) {
        ok = (err == Solid::NoError);
        if (!ok) {
            failure = data.toString();
            if (failure.isEmpty())
                failure = i18n("The device reported error %1.", int(err));
        }
        loop.quit();
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&]() {
        failure = i18n("Timed out waiting for the device to mount.");
        loop.quit();
    });
    timeout.start(45000);                      // 45 s - floppies are slow

    if (!access->setup()) {
        *errMsg = i18n("Could not start mounting the device.");
        return false;
    }
    loop.exec();

    if (!ok) { *errMsg = failure; return false; }
    *mountPath = access->filePath();
    if (mountPath->isEmpty()) {
        *errMsg = i18n("The device mounted but reported no location.");
        return false;
    }
    return true;
}

/* Join a mount point and a path below it, tolerating either having or
   missing the separator, and returning the mount point itself when there is
   nothing below it. */
static QString joinPath(const QString &base, const QString &rest)
{
    if (rest.isEmpty())
        return base;
    if (base.endsWith(QLatin1Char('/')))
        return base + rest;
    return base + QLatin1Char('/') + rest;
}

/* Drives the user unmounted on purpose, as recorded by Dolphin. Read fresh
   each time: the worker is long-lived and the list changes underneath it. */
static bool userUnmounted(const QString &udi)
{
    /* Parsed by hand rather than with QSettings or KConfig. KConfig would be
       another dependency for one list of strings; QSettings looks like the
       cheap answer and is not - dolphinrc opens with KDE's window geometry
       keys before any [section] header, which QSettings treats as a malformed
       file and gives up on, so every lookup came back empty and a drive the
       user had unmounted opened anyway. Read fresh each time: the worker
       outlives the setting. */
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QLatin1String("/dolphinrc");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    bool inGroup = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inGroup = (line == QLatin1String("[Exxos]"));
            continue;
        }
        if (!inGroup || !line.startsWith(QLatin1String("UnmountedByUser="))) {
            continue;
        }
        const QString value = line.section(QLatin1Char('='), 1);
        return value.split(QLatin1Char(','), Qt::SkipEmptyParts).contains(udi);
    }
    return false;
}

void ComputerProtocol::listDir(const QUrl &url)
{
    /* A path below the root names one drive.  computer:/ has no real
       sub-directories -- drives are links out -- so this mounts the drive if
       necessary and redirects the client to the real location. */
    if (url.path().length() > 1) {
        /* Anything below a drive belongs to the drive, not to us.  A search
           started from computer:/ builds its results by appending names to
           the URL it was given, so paths like
               computer:/<device>/midi/abracad.mid
           come back out and every program handed one says "file not found".
           Split the first segment off, match the drive on that, and redirect
           to the real location with the remainder kept. */
        const QString whole = url.path().mid(1);
        const int slash = whole.indexOf(QLatin1Char('/'));
        const QString wanted = slash < 0 ? whole : whole.left(slash);
        const QString remainder = slash < 0 ? QString() : whole.mid(slash + 1);

        if (wanted == QLatin1String("network-browse")) {
            redirection(QUrl(QStringLiteral("remote:/")));
            finished();
            return;
        }

        for (const NetworkPlace &p : networkPlaces()) {
            if (p.id == wanted) {
                redirection(QUrl(p.url));
                finished();
                return;
            }
        }

        for (const Drive &d : enumerateDrives()) {
            QString name = d.udi;
            name.replace(QLatin1Char('/'), QLatin1Char('_'));
            if (name != wanted)
                continue;

            if (d.network && !d.mountPath.isEmpty()) {
                redirection(QUrl::fromLocalFile(joinPath(d.mountPath, remainder)));
                finished();
                return;
            }

            QString path = d.mountPath, why;

            /* A drive the user unmounted stays unmounted. Opening it used to
               mount it again silently, which made Unmount look broken - the
               drive said "not mounted" and opened anyway. Dolphin records the
               deliberate unmounts in dolphinrc; Mount from the context menu
               clears the entry. */
            if (path.isEmpty() && userUnmounted(d.udi)) {
                error(KIO::ERR_ACCESS_DENIED,
                      i18n("%1 is not mounted. Right-click it and choose Mount to open it.",
                           d.label.isEmpty() ? d.description : d.label));
                return;
            }

            if (path.isEmpty() && !mountDrive(d.udi, &path, &why)) {
                error(KIO::ERR_CANNOT_MOUNT,
                      i18n("%1 could not be mounted. %2", d.label, why));
                return;
            }
            redirection(QUrl::fromLocalFile(joinPath(path, remainder)));
            finished();
            return;
        }

        error(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        return;
    }

    const auto drives = enumerateDrives();

    /* Explorer shows the item count in each heading, so the categories are
       tallied before anything is emitted.  The Network Locations tally starts
       at 1 to account for the Network shortcut appended below. */
    const auto places = networkPlaces();
    int tally[3] = { 0, 0, 1 + places.count() };
    for (const Drive &d : drives)
        ++tally[categoryKey(d)];

    for (const Drive &d : drives)
        listEntry(driveEntry(d, tally[categoryKey(d)]));

    for (const NetworkPlace &p : places)
        listEntry(networkPlaceEntry(p, tally[2]));

    listEntry(networkShortcut(tally[2]));

    listEntry(KIO::UDSEntry());   // end-of-listing marker
    finished();
}

void ComputerProtocol::stat(const QUrl &url)
{
    if (url.path().length() <= 1) {
        statEntry(rootEntry());
        finished();
        return;
    }

    const QString whole = url.path().mid(1);
    const int slash = whole.indexOf(QLatin1Char('/'));
    const QString wanted = slash < 0 ? whole : whole.left(slash);
    const QString remainder = slash < 0 ? QString() : whole.mid(slash + 1);

    if (wanted == QLatin1String("network-browse")) {
        statEntry(networkShortcut());
        finished();
        return;
    }

    for (const NetworkPlace &p : networkPlaces()) {
        if (p.id == wanted) {
            statEntry(networkPlaceEntry(p));
            finished();
            return;
        }
    }

    const auto drives = enumerateDrives();
    for (const Drive &d : drives) {
        QString name = d.udi;
        name.replace(QLatin1Char('/'), QLatin1Char('_'));
        if (name == wanted) {
            if (!remainder.isEmpty()) {
                /* Redirect rather than stat it ourselves: the file lives on a
                   real filesystem and kio_file knows far more about it. */
                redirection(QUrl::fromLocalFile(joinPath(d.mountPath, remainder)));
                finished();
                return;
            }
            statEntry(driveEntry(d));
            finished();
            return;
        }
    }
    error(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
}

} // namespace

extern "C" Q_DECL_EXPORT int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_computer"));

    /* Without this every i18n() call here logs
           kf.i18n: KLocalizedString: Using an empty domain, fix the code.
       to ~/.xsession-errors -- one line per group heading, per device, per
       listing. It was by far the noisiest thing in the session log. The
       strings still come out in English either way; the domain just tells
       KLocalizedString which catalogue to look in, and naming one that has no
       catalogue installed is fine and silent. */
    KLocalizedString::setApplicationDomain("kio_computer");

    if (argc != 4) {
        fprintf(stderr, "Usage: kio_computer protocol domain-socket1 domain-socket2\n");
        return -1;
    }

    ComputerProtocol slave(argv[2], argv[3]);
    slave.dispatchLoop();
    return 0;
}
