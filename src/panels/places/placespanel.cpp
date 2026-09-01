/*
 * SPDX-FileCopyrightText: 2008-2012 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2021 Kai Uwe Broulik <kde@broulik.de>
 *
 * Based on KFilePlacesView from kdelibs:
 * SPDX-FileCopyrightText: 2007 Kevin Ottens <ervin@kde.org>
 * SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "placespanel.h"

#include "dolphinplacesmodelsingleton.h"
#include "dolphin_generalsettings.h"
#include "dolphin_placespanelsettings.h"
#include "global.h"
#include "views/draganddrophelper.h"
#include "settings/dolphinsettingsdialog.h"

#include <KFilePlacesModel>
#include <KIO/DropJob>
#include <KIO/Job>
#include <KListOpenFilesJob>
#include <KLocalizedString>
#include <KProtocolManager>

#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QShowEvent>

#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/OpticalDrive>
#include <Solid/DeviceNotifier>
#include <QSet>

#include <QAbstractItemDelegate>
#include <QPainter>
#include <KFilePlacesModel>


/* ---------------------------------------------------------------------------
   Exxos/Win7: Places panel appearance.

   Dolphin 22.12 replaced its own KItemViews-based places widget with KIO's
   KFilePlacesView, so the previous patch (placesitemlistwidget.cpp) had nowhere
   to land. Two things had to be recovered, and KIO's delegate is private, so
   neither can be reached by subclassing it.

   The obstacle is that KIO's delegate uses QPalette::Highlight for BOTH the
   selected-row background AND the capacity bar fill. On this desktop Highlight
   is a grey, deliberately, so the capacity bars came out grey-on-grey and were
   almost invisible -- the same collision the original patch had to solve.

   A QProxyStyle cannot help: the delegate calls QApplication::style() with no
   widget argument, so there is nothing to discriminate on. Instead this wraps
   the delegate. We paint the row background ourselves, hand KIO's delegate a
   state with Selected/MouseOver cleared so it does not paint one on top, and
   set Highlight to the purple we actually want for the bar.
   --------------------------------------------------------------------------- */
namespace {

static const QColor EXXOS_CAPACITY_USED(108,  68, 158);   // purple, deep enough to read
static const QColor EXXOS_CAPACITY_SELECTED(78, 44, 120); // darker still, on a selected row
static const qreal  EXXOS_HOVER_ALPHA = 0.55;             // matches the file view

// Mirrors KFilePlacesViewDelegate's own header geometry, which is private.
static constexpr int s_lateralMargin = 4;

class ExxosPlacesDelegate : public QAbstractItemDelegate
{
public:
    ExxosPlacesDelegate(QAbstractItemDelegate *inner, QListView *view)
        : QAbstractItemDelegate(view), m_inner(inner), m_view(view) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return m_inner->sizeHint(option, index);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered  = opt.state & QStyle::State_MouseOver;

        /* The row rect includes the section header when this item starts a
           group, and the header must not sit on the selection wash -- so trim
           it off exactly the way KIO's delegate does before it paints. */
        QRect rowRect = opt.rect;
        if (indexIsSectionHeader(index)) {
            rowRect.setTop(rowRect.top() + sectionHeaderHeight(index));
        }

        if (selected || hovered) {
            QColor c = option.palette.color(QPalette::Highlight);
            if (!selected) {
                c.setAlphaF(EXXOS_HOVER_ALPHA);   // hover is a lighter wash of the same grey
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawRoundedRect(rowRect, 3, 3);
            painter->restore();
        }

        // Stop KIO drawing its own background over ours.
        opt.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver);
        // ...and give the capacity bar a colour of its own.
        opt.palette.setColor(QPalette::Highlight,
                             selected ? EXXOS_CAPACITY_SELECTED : EXXOS_CAPACITY_USED);

        m_inner->paint(painter, opt, index);

        /* Exxos/Win7: repaint the section header ("Places", "Remote", "Recent",
           "Search For", "Devices") in full-strength text.

           KIO draws it as mixedColor(WindowText, Window, 60) -- 60% text, 40%
           background -- which on this desktop is a light grey on a barely
           darker grey and is close to unreadable. The colour is computed inside
           its private drawSectionHeader(), so the only way at it is to paint
           over: fill the header strip back to the panel background, then draw
           the same string at full weight. Geometry is KIO's own, recomputed
           above, so the text lands exactly where it was. */
        if (indexIsSectionHeader(index)) {
            const QString label = index.data(KFilePlacesModel::GroupRole).toString();
            if (!label.isEmpty()) {
                QRect headerRect(option.rect);
                headerRect.setHeight(sectionHeaderHeight(index));

                QRect textRect(headerRect);
                textRect.setLeft(textRect.left() + 3);
                textRect.setHeight(sectionHeaderHeight(index) - s_lateralMargin - m_view->spacing());

                painter->save();
                painter->fillRect(headerRect, option.palette.color(QPalette::Window));
                painter->setPen(option.palette.color(QPalette::WindowText));
                painter->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom,
                                  option.fontMetrics.elidedText(label, Qt::ElideRight, textRect.width()));
                painter->restore();
            }
        }
    }

private:
    QString groupName(const QModelIndex &index) const
    {
        return index.isValid() ? index.data(KFilePlacesModel::GroupRole).toString() : QString();
    }

    QModelIndex previousVisibleIndex(const QModelIndex &index) const
    {
        if (!index.isValid() || index.row() == 0) {
            return QModelIndex();
        }
        const QAbstractItemModel *model = index.model();
        QModelIndex prev = model->index(index.row() - 1, index.column(), index.parent());
        while (m_view->isRowHidden(prev.row())) {
            if (prev.row() == 0) {
                return QModelIndex();
            }
            prev = model->index(prev.row() - 1, index.column(), index.parent());
        }
        return prev;
    }

    bool indexIsSectionHeader(const QModelIndex &index) const
    {
        if (m_view->isRowHidden(index.row())) {
            return false;
        }
        return groupName(index) != groupName(previousVisibleIndex(index));
    }

    int sectionHeaderHeight(const QModelIndex &index) const
    {
        const int spacing = s_lateralMargin + m_view->spacing();
        int height = m_view->fontMetrics().height() + spacing;
        if (index.row() != 0) {
            height += 2 * spacing;
        }
        return height;
    }

    QAbstractItemDelegate *m_inner;
    QListView *m_view;
};

} // namespace

PlacesPanel::PlacesPanel(QWidget* parent)
    : KFilePlacesView(parent)
{
    setDropOnPlaceEnabled(true);
    connect(this, &PlacesPanel::urlsDropped,
            this, &PlacesPanel::slotUrlsDropped);

    setAutoResizeItemsEnabled(false);

    /* Exxos/Win7: wrap KIO's delegate. KFilePlacesView installs its own in its
       constructor and keeps a private pointer for its animations, so replacing
       the VIEW's delegate changes only what gets painted. */
    if (QAbstractItemDelegate *inner = itemDelegate()) {
        setItemDelegate(new ExxosPlacesDelegate(inner, this));
    }

    setTeardownFunction([this](const QModelIndex &index) {
        slotTearDownRequested(index);
    });

    m_configureTrashAction = new QAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Configure Trash…"));
    m_configureTrashAction->setPriority(QAction::HighPriority);
    connect(m_configureTrashAction, &QAction::triggered, this, &PlacesPanel::slotConfigureTrash);
    addAction(m_configureTrashAction);

    connect(this, &PlacesPanel::contextMenuAboutToShow, this, &PlacesPanel::slotContextMenuAboutToShow);

    connect(this, &PlacesPanel::iconSizeChanged, this, [this](const QSize &newSize) {
        int iconSize = qMin(newSize.width(), newSize.height());
        if (iconSize == 0) {
            // Don't store 0 size, let's keep -1 for default/small/automatic
            iconSize = -1;
        }
        PlacesPanelSettings* settings = PlacesPanelSettings::self();
        settings->setIconSize(iconSize);
        settings->save();
    });
}

PlacesPanel::~PlacesPanel() = default;

void PlacesPanel::setUrl(const QUrl &url)
{
    // KFilePlacesView::setUrl no-ops when no model is set but we only set it in showEvent()
    // Remember the URL and set it in showEvent
    m_url = url;
    KFilePlacesView::setUrl(url);
}

QList<QAction*> PlacesPanel::customContextMenuActions() const
{
    return m_customContextMenuActions;
}

void PlacesPanel::setCustomContextMenuActions(const QList<QAction *> &actions)
{
    m_customContextMenuActions = actions;
}

void PlacesPanel::proceedWithTearDown()
{
    Q_ASSERT(m_deviceToTearDown);

    connect(m_deviceToTearDown, &Solid::StorageAccess::teardownDone,
            this, &PlacesPanel::slotTearDownDone);
    m_deviceToTearDown->teardown();
}

void PlacesPanel::readSettings()
{
    if (GeneralSettings::autoExpandFolders()) {
        setDragAutoActivationDelay(750);
    } else {
        setDragAutoActivationDelay(0);
    }

    const int iconSize = qMax(0, PlacesPanelSettings::iconSize());
    setIconSize(QSize(iconSize, iconSize));
}

/* --------------------------------------------------------------------------
   Exxos/Win7: list drive HARDWARE, not only mountable volumes.

   KFilePlacesModel lists devices that expose a volume. Hardware with no medium
   in it does not: an empty CD/DVD tray, an empty card-reader slot. So the CD
   drive was absent from this panel entirely -- the same gap the computer:/
   worker had, in code that is KIO's rather than ours.

   The panel's model cannot be swapped for a proxy: KIO's delegate static_casts
   it to KFilePlacesModel, so anything else is undefined behaviour. But the
   model does take ordinary places through addPlace(), and each drive already
   has a stable address in our own worker -- computer:/<udi> -- which mounts on
   open. So an empty bay is added as a place pointing there, and clicking it
   behaves exactly like clicking it in the main view.

   Entries are matched on that URL, so re-running is harmless and a bay that
   already has a volume (the model lists it itself) is never duplicated.
   -------------------------------------------------------------------------- */
void PlacesPanel::syncHardwarePlaces()
{
    auto *placesModel = qobject_cast<KFilePlacesModel *>(model());
    if (!placesModel) {
        return;
    }

    /* Drives that already have a volume, so the model shows them itself.

       Do NOT assume the volume's immediate parent is the StorageDrive. It is
       for a partition on a disk, but an optical disc is its own device under
       the drive, so with a disc inserted coverage was never recorded and the
       CD appeared twice -- once as its volume label under Removable Devices
       and again as the empty bay we had added. Walk up to the drive. */
    QSet<QString> coveredDrives;
    const auto volumes = Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess, QString());
    for (const Solid::Device &dev : volumes) {
        Solid::Device p = dev.parent();
        for (int depth = 0; p.isValid() && depth < 6; ++depth) {
            if (p.is<Solid::StorageDrive>()) {
                coveredDrives.insert(p.udi());
                break;
            }
            p = p.parent();
        }
    }

    // Every bay currently present, as the URL it would be listed under.
    QSet<QString> liveBays;
    {
        const auto all = Solid::Device::listFromType(Solid::DeviceInterface::StorageDrive, QString());
        for (const Solid::Device &dev : all) {
            QString n = dev.udi();
            n.replace(QLatin1Char('/'), QLatin1Char('_'));
            liveBays.insert(QStringLiteral("computer:/") + n);
        }
    }

    /* Drop entries we added for hardware that has since gone, and for bays
       that have acquired a volume -- the model lists those itself now, and
       leaving ours behind would show the drive twice. addPlace() writes a
       persistent bookmark, so without this they would pile up every time a
       USB device was unplugged. */
    for (int i = placesModel->rowCount() - 1; i >= 0; --i) {
        const QModelIndex idx = placesModel->index(i, 0);
        const QString u = placesModel->url(idx).toString();
        /* ONLY entries this code generated.
           Must not be a plain startsWith("computer:/") test: the user's own
           "Computer" place IS computer:/ , and matching it deleted a bookmark
           that was never ours to touch. Ours always address a specific bay, so
           the path is a sanitised Solid UDI and begins with an underscore
           (from the leading slash of /org/freedesktop/...). */
        if (!u.startsWith(QLatin1String("computer:/_"))) {
            continue;
        }
        const bool goneOrMounted = !liveBays.contains(u)
            || [&] {
                   for (const QString &covered : coveredDrives) {
                       QString n = covered;
                       n.replace(QLatin1Char('/'), QLatin1Char('_'));
                       if (u == QStringLiteral("computer:/") + n) {
                           return true;
                       }
                   }
                   return false;
               }();
        if (goneOrMounted) {
            placesModel->removePlace(idx);
        }
    }

    // URLs this panel already carries, so nothing is added twice.
    QSet<QString> existing;
    for (int i = 0; i < placesModel->rowCount(); ++i) {
        existing.insert(placesModel->url(placesModel->index(i, 0)).toString());
    }

    const auto drives = Solid::Device::listFromType(Solid::DeviceInterface::StorageDrive, QString());
    for (const Solid::Device &dev : drives) {
        const auto *drive = dev.as<Solid::StorageDrive>();
        if (!drive) {
            continue;
        }
        const bool isOptical = dev.is<Solid::OpticalDrive>();
        if (!isOptical && !(drive->isRemovable() || drive->isHotpluggable())) {
            continue;   // an internal disk with no volume is not worth listing
        }
        if (coveredDrives.contains(dev.udi())) {
            continue;
        }

        /* Same address the computer:/ worker gives it: the UDI with slashes
           replaced, because a UDS_NAME may not contain one. */
        QString name = dev.udi();
        name.replace(QLatin1Char('/'), QLatin1Char('_'));
        const QUrl url(QStringLiteral("computer:/") + name);
        if (existing.contains(url.toString())) {
            continue;
        }

        QString label = dev.displayName();
        if (label.isEmpty()) {
            label = isOptical ? i18n("CD/DVD Drive") : i18n("Removable Drive");
        }
        placesModel->addPlace(label, url,
                              isOptical ? QStringLiteral("drive-optical")
                                        : QStringLiteral("drive-removable-media"));
    }
}

void PlacesPanel::showEvent(QShowEvent* event)
{
    if (!event->spontaneous() && !model()) {
        readSettings();

        auto *placesModel = DolphinPlacesModelSingleton::instance().placesModel();
        setModel(placesModel);

        connect(placesModel, &KFilePlacesModel::errorMessage, this, &PlacesPanel::errorMessage);

        connect(placesModel, &QAbstractItemModel::rowsInserted, this, &PlacesPanel::slotRowsInserted);
        connect(placesModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PlacesPanel::slotRowsAboutToBeRemoved);

        for (int i = 0; i < model()->rowCount(); ++i) {
            connectDeviceSignals(model()->index(i, 0, QModelIndex()));
        }

        setUrl(m_url);
        /* syncHardwarePlaces() is DISABLED.

           It worked -- empty bays appeared and were clickable -- but they landed
           under "Places" rather than "Removable Devices", and that cannot be
           corrected from here. KFilePlacesItem::groupType() only returns
           RemovableDevicesType when the item is a device, an item is a device
           only when its bookmark carries a UDI, and KFilePlacesModel drops a
           UDI-carrying bookmark unless the device passes a predicate requiring
           a StorageVolume or StorageAccess -- which an empty bay fails by
           definition. Grouping it correctly and showing it while empty are the
           same flag.

           An entry in visibly the wrong section is worse than no entry, so it
           stays off until this is done properly, which means replacing
           KFilePlacesModel rather than extending it. The code is kept because
           the analysis behind it is worth not repeating.

           syncHardwarePlaces();  */

        /* Re-sync whenever hardware appears or disappears. Inserting a disc
           gives the drive a volume, so the model lists it itself and our
           empty-bay entry has to go, or the drive shows twice. Ejecting undoes
           that. Doing this only on showEvent left both entries on screen for
           as long as the panel stayed open. */
        connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceAdded,
                this, [this](const QString &) { syncHardwarePlaces(); });
        connect(Solid::DeviceNotifier::instance(), &Solid::DeviceNotifier::deviceRemoved,
                this, [this](const QString &) { syncHardwarePlaces(); });
    }

    KFilePlacesView::showEvent(event);
}

static bool isInternalDrag(const QMimeData *mimeData)
{
    const auto formats = mimeData->formats();
    for (const auto &format : formats) {
        // from KFilePlacesModel::_k_internalMimetype
        if (format.startsWith(QLatin1String("application/x-kfileplacesmodel-"))) {
            return true;
        }
    }
    return false;
}

void PlacesPanel::dragMoveEvent(QDragMoveEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid()) {
        auto *placesModel = static_cast<KFilePlacesModel *>(model());

        // Reject drag ontop of a non-writable protocol
        // We don't know whether we're dropping inbetween or ontop of a place
        // so still allow internal drag events so that re-arranging still works.
        const QUrl url = placesModel->url(index);
        if (url.isValid() && !isInternalDrag(event->mimeData()) && !KProtocolManager::supportsWriting(url)) {
            event->setDropAction(Qt::IgnoreAction);
        }
    }

    KFilePlacesView::dragMoveEvent(event);
}

void PlacesPanel::slotConfigureTrash()
{
    const QUrl url = currentIndex().data(KFilePlacesModel::UrlRole).toUrl();

    DolphinSettingsDialog* settingsDialog = new DolphinSettingsDialog(url, this);
    settingsDialog->setCurrentPage(settingsDialog->trashSettings);
    settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog->show();
}

void PlacesPanel::slotUrlsDropped(const QUrl& dest, QDropEvent* event, QWidget* parent)
{
    KIO::DropJob *job = DragAndDropHelper::dropUrls(dest, event, parent);
    if (job) {
        connect(job, &KIO::DropJob::result, this, [this](KJob *job) {
            if (job->error() && job->error() != KIO::ERR_USER_CANCELED) {
                Q_EMIT errorMessage(job->errorString());
            }
        });
    }
}

void PlacesPanel::slotContextMenuAboutToShow(const QModelIndex &index, QMenu *menu)
{
    Q_UNUSED(menu);

    auto *placesModel = static_cast<KFilePlacesModel *>(model());
    const QUrl url = placesModel->url(index);
    const Solid::Device device = placesModel->deviceForIndex(index);

    m_configureTrashAction->setVisible(url.scheme() == QLatin1String("trash"));

    // show customContextMenuActions only on the view's context menu
    if (!url.isValid() && !device.isValid()) {
        addActions(m_customContextMenuActions);
    } else {
        const auto actions = this->actions();
        for (QAction *action : actions) {
            if (m_customContextMenuActions.contains(action)) {
                removeAction(action);
            }
        }
    }
}

void PlacesPanel::slotTearDownRequested(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    m_deviceToTearDown = storageAccess;

    // disconnect the Solid::StorageAccess::teardownRequested
    // to prevent emitting PlacesPanel::storageTearDownExternallyRequested
    // after we have emitted PlacesPanel::storageTearDownRequested
    disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
    Q_EMIT storageTearDownRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownRequestedExternally(const QString &udi)
{
    Q_UNUSED(udi);
    auto *storageAccess = static_cast<Solid::StorageAccess*>(sender());

    Q_EMIT storageTearDownExternallyRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownDone(Solid::ErrorType error, const QVariant& errorData)
{
    if (error && errorData.isValid()) {
        if (error == Solid::ErrorType::UserCanceled) {
            // No need to tell the user what they just did.
        } else if (error == Solid::ErrorType::DeviceBusy) {
            KListOpenFilesJob* listOpenFilesJob = new KListOpenFilesJob(m_deviceToTearDown->filePath());
            connect(listOpenFilesJob, &KIO::Job::result, this, [this, listOpenFilesJob](KJob*) {
                const KProcessList::KProcessInfoList blockingProcesses = listOpenFilesJob->processInfoList();
                QString errorString;
                if (blockingProcesses.isEmpty()) {
                    errorString = i18n("One or more files on this device are open within an application.");
                } else {
                    QStringList blockingApps;
                    for (const auto& process : blockingProcesses) {
                        blockingApps << process.name();
                    }
                    blockingApps.removeDuplicates();
                    errorString = xi18np("One or more files on this device are opened in application <application>\"%2\"</application>.",
                            "One or more files on this device are opened in following applications: <application>%2</application>.",
                            blockingApps.count(), blockingApps.join(i18nc("separator in list of apps blocking device unmount", ", ")));
                }
                Q_EMIT errorMessage(errorString);
            });
            listOpenFilesJob->start();
        } else {
            Q_EMIT errorMessage(errorData.toString());
        }
    } else {
        // No error; it must have been unmounted successfully
        Q_EMIT storageTearDownSuccessful();
    }
    disconnect(m_deviceToTearDown, &Solid::StorageAccess::teardownDone,
               this, &PlacesPanel::slotTearDownDone);
    m_deviceToTearDown = nullptr;
}

void PlacesPanel::slotRowsInserted(const QModelIndex &parent, int first, int last)
{
    for (int i = first; i <= last; ++i) {
        connectDeviceSignals(model()->index(first, 0, parent));
    }
}

void PlacesPanel::slotRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    for (int i = first; i <= last; ++i) {
        const QModelIndex index = placesModel->index(i, 0, parent);

        Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
        if (!storageAccess) {
            continue;
        }

        disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, nullptr);
    }
}

void PlacesPanel::connectDeviceSignals(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    connect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
}
