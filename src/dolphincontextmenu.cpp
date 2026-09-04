/*
 * SPDX-FileCopyrightText: 2006 Peter Penz (peter.penz@gmx.at) and Cvetoslav Ludmiloff
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphincontextmenu.h"

#include "dolphin_generalsettings.h"
#include "dolphin_contextmenusettings.h"
#include "dolphinmainwindow.h"
#include "dolphinnewfilemenu.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinremoveaction.h"
#include "dolphinviewcontainer.h"
#include "global.h"
#include "trash/dolphintrash.h"
#include "views/dolphinview.h"
#include "views/viewmodecontroller.h"

#include <KActionCollection>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KFileItemListProperties>
#include <KHamburgerMenu>
#include <KIO/CopyJob>
#include <KIO/EmptyTrashJob>
#include <KIO/JobUiDelegate>
#include <KIO/Paste>
#include <KIO/RestoreJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KNewFileMenu>
#include <KStandardAction>
#include <kio_version.h>

#include <KIO/UDSEntry>

#include <KMessageBox>
#include <QPointer>
#include <Solid/Device>
#include <Solid/OpticalDrive>
#include "exxosmediarescan.h"
#include <Solid/StorageAccess>

#include <QApplication>
#include <QClipboard>
#include <QStandardPaths>
#include <QKeyEvent>

DolphinContextMenu::DolphinContextMenu(DolphinMainWindow* parent,
                                       const KFileItem& fileInfo,
                                       const KFileItemList &selectedItems,
                                       const QUrl& baseUrl,
                                       KFileItemActions *fileItemActions) :
    QMenu(parent),
    m_mainWindow(parent),
    m_fileInfo(fileInfo),
    m_baseUrl(baseUrl),
    m_baseFileItem(nullptr),
    m_selectedItems(selectedItems),
    m_selectedItemsProperties(nullptr),
    m_context(NoContext),
    m_copyToMenu(parent),
    m_removeAction(nullptr),
    m_fileItemActions(fileItemActions)
{
    QApplication::instance()->installEventFilter(this);

    addAllActions();
}

DolphinContextMenu::~DolphinContextMenu()
{
    delete m_baseFileItem;
    m_baseFileItem = nullptr;
    delete m_selectedItemsProperties;
    m_selectedItemsProperties = nullptr;
}

void DolphinContextMenu::addAllActions()
{
    static_cast<KHamburgerMenu *>(m_mainWindow->actionCollection()->
                action(QStringLiteral("hamburger_menu")))->addToMenu(this);

    // get the context information
    const auto scheme = m_baseUrl.scheme();
    if (scheme == QLatin1String("trash")) {
        m_context |= TrashContext;
    } else if (scheme.contains(QLatin1String("search"))) {
        m_context |= SearchContext;
    } else if (scheme.contains(QLatin1String("timeline"))) {
        m_context |= TimelineContext;
    }

    if (!m_fileInfo.isNull() && !m_selectedItems.isEmpty()) {
        m_context |= ItemContext;
        // TODO: handle other use cases like devices + desktop files
    }

    // open the corresponding popup for the context
    if (m_context & TrashContext) {
        if (m_context & ItemContext) {
            addTrashItemContextMenu();
        } else {
            addTrashContextMenu();
        }
    } else if (m_context & ItemContext) {
        addItemContextMenu();
    } else {
        addViewportContextMenu();
    }
}

/* Exxos/Win7: drives the user has deliberately unmounted.

   Kept in dolphinrc so it survives a restart, and read by the computer:/
   worker too - which is what stops a click on an unmounted drive quietly
   mounting it again. Solid UDIs are stable, so they are what is stored. */
void exxosSetUnmountedByUser(const QString &udi, bool unmounted)
{
    KConfigGroup group(KSharedConfig::openConfig(), "Exxos");
    QStringList list = group.readEntry("UnmountedByUser", QStringList());
    const bool had = list.contains(udi);
    if (unmounted && !had) {
        list.append(udi);
    } else if (!unmounted && had) {
        list.removeAll(udi);
    } else {
        return;
    }
    group.writeEntry("UnmountedByUser", list);
    group.sync();
}

bool DolphinContextMenu::eventFilter(QObject* object, QEvent* event)
{
    Q_UNUSED(object)

    if(event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        if (m_removeAction && keyEvent->key() == Qt::Key_Shift) {
            if (event->type() == QEvent::KeyPress) {
                m_removeAction->update(DolphinRemoveAction::ShiftState::Pressed);
            } else {
                m_removeAction->update(DolphinRemoveAction::ShiftState::Released);
            }
        }
    }

    return false;
}

void DolphinContextMenu::addTrashContextMenu()
{
    Q_ASSERT(m_context & TrashContext);

    QAction *emptyTrashAction = addAction(QIcon::fromTheme(QStringLiteral("trash-empty")), i18nc("@action:inmenu", "Empty Trash"), [this](){
        Trash::empty(m_mainWindow);
    });
    emptyTrashAction->setEnabled(!Trash::isEmpty());

    QAction* propertiesAction = m_mainWindow->actionCollection()->action(QStringLiteral("properties"));
    addAction(propertiesAction);
}

void DolphinContextMenu::addTrashItemContextMenu()
{
    Q_ASSERT(m_context & TrashContext);
    Q_ASSERT(m_context & ItemContext);

    addAction(QIcon::fromTheme("restoration"), i18nc("@action:inmenu", "Restore"), [this](){
        QList<QUrl> selectedUrls;
        selectedUrls.reserve(m_selectedItems.count());
        for (const KFileItem &item : qAsConst(m_selectedItems)) {
            selectedUrls.append(item.url());
        }

        KIO::RestoreJob *job = KIO::restoreFromTrash(selectedUrls);
        KJobWidgets::setWindow(job, m_mainWindow);
        job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    });

    QAction* deleteAction = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::DeleteFile));
    addAction(deleteAction);

    QAction* propertiesAction = m_mainWindow->actionCollection()->action(QStringLiteral("properties"));
    addAction(propertiesAction);
}

void DolphinContextMenu::addDirectoryItemContextMenu()
{
    // insert 'Open in new window' and 'Open in new tab' entries
    const KFileItemListProperties& selectedItemsProps = selectedItemsProperties();
    if (ContextMenuSettings::showOpenInNewTab()) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("open_in_new_tab")));
    }
    if (ContextMenuSettings::showOpenInNewWindow()) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("open_in_new_window")));
    }

    // Insert 'Open With' entries
    addOpenWithActions();

    // set up 'Create New' menu
    DolphinNewFileMenu* newFileMenu = new DolphinNewFileMenu(m_mainWindow->actionCollection(), m_mainWindow);
    newFileMenu->checkUpToDate();
#if KIO_VERSION >= QT_VERSION_CHECK(5, 97, 0)
    newFileMenu->setWorkingDirectory(m_fileInfo.url());
#else
    newFileMenu->setPopupFiles(QList<QUrl>() << m_fileInfo.url());
#endif
    newFileMenu->setEnabled(selectedItemsProps.supportsWriting());
    connect(newFileMenu, &DolphinNewFileMenu::fileCreated, newFileMenu, &DolphinNewFileMenu::deleteLater);
    connect(newFileMenu, &DolphinNewFileMenu::directoryCreated, newFileMenu, &DolphinNewFileMenu::deleteLater);

    QMenu* menu = newFileMenu->menu();
    menu->setTitle(i18nc("@title:menu Create new folder, file, link, etc.", "Create New"));
    menu->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    addMenu(menu);

    addSeparator();
}

/* Exxos/Win7: act on a drive from the computer:/ icon view.

   WHY THIS EXISTS.  Auto-mount is off by default, and deliberately so -- see
   DolphinMainWindow::slotToggleAutoMount() for the reason.  With it off there
   was no way to mount a drive except to open it, which is not discoverable and
   gives no way to UNMOUNT or eject one again.  Windows shows Open / Eject on a
   drive's context menu; this is that.

   The Solid UDI comes over in UDS_EXTRA+2 rather than being reconstructed from
   the item name: computer:/ builds the name by replacing '/' with '_' in the
   UDI, and real UDIs contain underscores ("block_devices"), so the mapping
   cannot be reversed.  UDS_EXTRA+3 carries the state and +4 the kind, so the
   menu offers exactly the operations that apply.

   Solid is used here in Dolphin's own process, which runs an event loop, so
   its device state is live -- unlike inside the worker (see refreshSolid() in
   kio-computer/computer.cpp).  The view refreshes itself afterwards through
   the accessibilityChanged / deviceRemoved handlers in DolphinView. */
bool DolphinContextMenu::addComputerDeviceActions()
{
    if (m_selectedItems.count() != 1 || m_fileInfo.isNull()) {
        return false;
    }
    if (m_fileInfo.url().scheme() != QLatin1String("computer")) {
        return false;
    }

    const KIO::UDSEntry entry = m_fileInfo.entry();
    const QString udi   = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 2);
    const QString state = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 3);
    const QString kind  = entry.stringValue(KIO::UDSEntry::UDS_EXTRA + 4);
    if (udi.isEmpty()) {
        return false;   // the Network shortcut, or an unpatched worker
    }

    bool added = false;

    if (state == QLatin1String("unmounted") || state == QLatin1String("locked")) {
        addAction(QIcon::fromTheme(QStringLiteral("media-mount")),
                  i18nc("@action:inmenu", "Mount"), [udi]() {
            /* Spin the drive while it happens. Mounting a slow disk takes a
               noticeable moment, and with no sign of anything happening the
               obvious conclusion is that the click missed. The spinner is
               stopped by accessibilityChanged when the drive is ready. */
            ExxosBusySpinner::instance()->setDriveBusy(udi, true);
            Solid::Device dev(udi);
            if (auto *access = const_cast<Solid::StorageAccess *>(dev.as<Solid::StorageAccess>())) {
                access->setup();
            }
            exxosSetUnmountedByUser(udi, false);
        });
        added = true;
    } else if (state == QLatin1String("mounted")) {
        addAction(QIcon::fromTheme(QStringLiteral("media-eject")),
                  i18nc("@action:inmenu", "Unmount"), [this, udi]() {
            ExxosBusySpinner::instance()->setDriveBusy(udi, true);
            Solid::Device dev(udi);
            auto *access = const_cast<Solid::StorageAccess *>(dev.as<Solid::StorageAccess>());
            const bool dbg = qEnvironmentVariableIsSet("EXXOS_MOUNT_DEBUG");
            if (dbg) {
                qWarning("exxos-unmount: udi=%s valid=%d access=%p accessible=%d",
                         qPrintable(udi), int(dev.isValid()), (void *)access,
                         access ? int(access->isAccessible()) : -1);
            }
            if (!access) {
                ExxosBusySpinner::instance()->setDriveBusy(udi, false);
                return;
            }

            /* Wait for the RESULT before recording anything. The record is
               what makes unmounting mean something -- without it the drive is
               mounted again by the next click on it, or by the auto-mount
               sweep six seconds later. But writing it straight after
               teardown() recorded the intention rather than the outcome: a
               drive that is busy, or one the user has no permission to
               unmount, stayed mounted and was marked unmounted anyway, so the
               padlock appeared over a drive that was still fully open. Found
               that way on this machine 2026-09-03. */
            QPointer<QWidget> parent(m_mainWindow);
            QPointer<DolphinMainWindow> window(m_mainWindow);
            auto *ctx = new QObject();
            connect(access, &Solid::StorageAccess::teardownDone, ctx,
                    [ctx, udi, parent, window](Solid::ErrorType error, const QVariant &message, const QString &) {
                ExxosBusySpinner::instance()->setDriveBusy(udi, false);
                if (error == Solid::NoError) {
                    exxosSetUnmountedByUser(udi, true);
                    /* Re-list AFTER the record exists. The view is already
                       being reloaded by accessibilityChanged, which fires as
                       soon as the volume goes away -- before this reply
                       arrives -- so that listing sees the drive as merely
                       "unmounted" and the padlock never appears. Measured
                       2026-09-03: the tile said "not mounted" with no padlock
                       while dolphinrc already held the drive. */
                    if (window && window->activeViewContainer()) {
                        window->activeViewContainer()->view()->reload();
                    }
                } else {
                    /* Say so. A drive that refuses to unmount because
                       something still has a file open on it is the ordinary
                       case, and silence there looks like the menu entry does
                       nothing at all. */
                    const QString text = message.toString();
                    KMessageBox::error(parent,
                        text.isEmpty()
                            ? i18nc("@info", "The drive could not be unmounted.")
                            : i18nc("@info", "The drive could not be unmounted: %1", text));
                }
                ctx->deleteLater();
            });
            const bool started = access->teardown();
            if (dbg) {
                qWarning("exxos-unmount: teardown() returned %d", int(started));
            }
            /* teardown() answering false means it never even asked, so no
               teardownDone is coming and the spinner would turn for ever. */
            if (!started) {
                ExxosBusySpinner::instance()->setDriveBusy(udi, false);
                ctx->deleteLater();
                KMessageBox::error(parent,
                    i18nc("@info", "The drive could not be unmounted."));
            }
        });
        added = true;
    }

    /* Eject applies to the tray itself, so it is offered whether or not there
       is a disc in it -- including on an empty bay, which is how the tray gets
       opened again after it has been closed. */
    if (kind == QLatin1String("optical")) {
        addAction(QIcon::fromTheme(QStringLiteral("media-optical")),
                  i18nc("@action:inmenu", "Eject"), [udi]() {
            /* The UDI may name the disc rather than the drive; the drive is
               the ancestor that carries the OpticalDrive interface. An empty
               bay's UDI already is the drive, and the walk stops at once. */
            Solid::Device dev(udi);
            while (dev.isValid() && !dev.is<Solid::OpticalDrive>()) {
                dev = dev.parent();
            }
            if (auto *drive = const_cast<Solid::OpticalDrive *>(dev.as<Solid::OpticalDrive>())) {
                drive->eject();
            }
        });
        added = true;
    }

    if (added) {
        addSeparator();
    }
    return added;
}

void DolphinContextMenu::addItemContextMenu()
{
    Q_ASSERT(!m_fileInfo.isNull());

    const KFileItemListProperties& selectedItemsProps = selectedItemsProperties();

    m_fileItemActions->setItemListProperties(selectedItemsProps);

    // Exxos: Mount / Unmount / Eject go at the top, above Open.
    addComputerDeviceActions();

    if (m_selectedItems.count() == 1) {
        // single files
        if (m_fileInfo.isDir()) {
            addDirectoryItemContextMenu();
        } else if (m_context & TimelineContext || m_context & SearchContext) {
            addOpenWithActions();

            addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")),
                                           i18nc("@action:inmenu",
                                                 "Open Containing Folder"),
                                           [this](){
                m_mainWindow->changeUrl(KIO::upUrl(m_fileInfo.url()));
                m_mainWindow->activeViewContainer()->view()->markUrlsAsSelected({m_fileInfo.url()});
                m_mainWindow->activeViewContainer()->view()->markUrlAsCurrent(m_fileInfo.url());
            });

            addAction(QIcon::fromTheme(QStringLiteral("window-new")),
                                                    i18nc("@action:inmenu",
                                                          "Open Containing Folder in New Window"),
                                                    [this](){
                Dolphin::openNewWindow({m_fileInfo.url()}, m_mainWindow, Dolphin::OpenNewWindowFlag::Select);
            });

            addAction(QIcon::fromTheme(QStringLiteral("tab-new")),
                                                   i18nc("@action:inmenu",
                                                         "Open Containing Folder in New Tab"),
                                                   [this](){
                m_mainWindow->openNewTab(KIO::upUrl(m_fileInfo.url()));
            });

            addSeparator();
        } else {
            // Insert 'Open With" entries
            addOpenWithActions();
        }
        if (m_fileInfo.isLink()) {
            addAction(m_mainWindow->actionCollection()->action(QStringLiteral("show_target")));
            addSeparator();
        }
    } else {
        // multiple files
        bool selectionHasOnlyDirs = true;
        for (const auto &item : qAsConst(m_selectedItems)) {
            const QUrl& url = DolphinView::openItemAsFolderUrl(item);
            if (url.isEmpty()) {
                selectionHasOnlyDirs = false;
                break;
            }
        }

        if (selectionHasOnlyDirs && ContextMenuSettings::showOpenInNewTab()) {
            // insert 'Open in new tab' entry
            addAction(m_mainWindow->actionCollection()->action(QStringLiteral("open_in_new_tabs")));
        }
        // Insert 'Open With" entries
        addOpenWithActions();
    }

    /* Exxos/Win7: "Send to Desktop (create shortcut)".

       Windows puts this under Send To, and it is the one entry from that menu
       people actually reach for. KIO::link is exactly the right operation: a
       symlink for a local file, a .desktop link for anything remote, with name
       clashes and error reporting already handled -- which is why this does not
       call symlink() itself. */
    addSendToDesktopAction();

    insertDefaultItemActions(selectedItemsProps);

    addAdditionalActions(selectedItemsProps);

    // insert 'Copy To' and 'Move To' sub menus
    if (ContextMenuSettings::showCopyMoveMenu()) {
        m_copyToMenu.setUrls(m_selectedItems.urlList());
        m_copyToMenu.setReadOnly(!selectedItemsProps.supportsWriting());
        m_copyToMenu.setAutoErrorHandlingEnabled(true);
        m_copyToMenu.addActionsTo(this);
    }

    // insert 'Properties...' entry
    addSeparator();
    QAction* propertiesAction = m_mainWindow->actionCollection()->action(QStringLiteral("properties"));
    addAction(propertiesAction);
}

void DolphinContextMenu::addSendToDesktopAction()
{
    if (m_selectedItems.isEmpty()) {
        return;
    }
    /* computer:/ entries are devices, not files -- there is nothing to link. */
    if (m_fileInfo.url().scheme() == QLatin1String("computer")) {
        return;
    }

    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktopPath.isEmpty()) {
        return;
    }
    const QUrl desktopUrl = QUrl::fromLocalFile(desktopPath);

    /* Offering it for something already sitting on the desktop just makes
       "file (link)" next to the file. */
    if (m_selectedItems.count() == 1
        && KIO::upUrl(m_fileInfo.url()).matches(desktopUrl, QUrl::StripTrailingSlash)) {
        return;
    }

    addSeparator();
    addAction(QIcon::fromTheme(QStringLiteral("emblem-symbolic-link")),
              i18nc("@action:inmenu", "Send to Desktop (create shortcut)"),
              [this, desktopUrl]() {
        KIO::CopyJob *job = KIO::link(m_selectedItems.urlList(), desktopUrl);
        KJobWidgets::setWindow(job, m_mainWindow);
        job->uiDelegate()->setAutoErrorHandlingEnabled(true);
    });
}

void DolphinContextMenu::addViewportContextMenu()
{
    const KFileItemListProperties baseUrlProperties(KFileItemList() << baseFileItem());
    m_fileItemActions->setItemListProperties(baseUrlProperties);

    /* Exxos: the auto-mount toggle also lives in Settings, but computer:/ is
       where it is wanted, and right-clicking the empty space there is a
       shorter route than remembering which menu it is under. */
    if (m_baseUrl.scheme() == QLatin1String("computer")) {
        if (QAction *autoMount = m_mainWindow->actionCollection()->action(QStringLiteral("exxos_auto_mount"))) {
            addAction(autoMount);
            addSeparator();
        }
    }

    // Set up and insert 'Create New' menu
    KNewFileMenu* newFileMenu = m_mainWindow->newFileMenu();
    newFileMenu->checkUpToDate();
#if KIO_VERSION >= QT_VERSION_CHECK(5, 97, 0)
    newFileMenu->setWorkingDirectory(m_baseUrl);
#else
    newFileMenu->setPopupFiles(QList<QUrl>() << m_baseUrl);
#endif
    addMenu(newFileMenu->menu());

    // Show "open with" menu items even if the dir is empty, because there are legitimate
    // use cases for this, such as opening an empty dir in Kate or VSCode or something
    addOpenWithActions();

    QAction* pasteAction = createPasteAction();
    if (pasteAction) {
        addAction(pasteAction);
    }

    // Insert 'Add to Places' entry if it's not already in the places panel
    if (ContextMenuSettings::showAddToPlaces() &&
            !placeExists(m_mainWindow->activeViewContainer()->url())) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("add_to_places")));
    }
    addSeparator();

    // Insert 'Sort By' and 'View Mode'
    if (ContextMenuSettings::showSortBy()) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("sort")));
    }
    if (ContextMenuSettings::showViewMode()) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("view_mode")));
    }
    if (ContextMenuSettings::showSortBy() || ContextMenuSettings::showViewMode()) {
        addSeparator();
    }

    addAdditionalActions(baseUrlProperties);

    addSeparator();

    QAction* propertiesAction = m_mainWindow->actionCollection()->action(QStringLiteral("properties"));
    addAction(propertiesAction);
}

void DolphinContextMenu::insertDefaultItemActions(const KFileItemListProperties& properties)
{
    const KActionCollection* collection = m_mainWindow->actionCollection();

    // Insert 'Cut', 'Copy', 'Copy Location' and 'Paste'
    addAction(collection->action(KStandardAction::name(KStandardAction::Cut)));
    addAction(collection->action(KStandardAction::name(KStandardAction::Copy)));
    if (ContextMenuSettings::showCopyLocation()) {
        QAction* copyPathAction = collection->action(QString("copy_location"));
        copyPathAction->setEnabled(m_selectedItems.size() == 1);
        addAction(copyPathAction);
    }
    QAction* pasteAction = createPasteAction();
    if (pasteAction) {
        addAction(pasteAction);
    }

    // Insert 'Duplicate Here'
    if (ContextMenuSettings::showDuplicateHere()) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("duplicate")));
    }

    // Insert 'Rename'
    addAction(collection->action(KStandardAction::name(KStandardAction::RenameFile)));

    // Insert 'Add to Places' entry if appropriate
    if (ContextMenuSettings::showAddToPlaces() &&
            m_selectedItems.count() == 1 &&
            m_fileInfo.isDir() &&
            !placeExists(m_fileInfo.url())) {
        addAction(m_mainWindow->actionCollection()->action(QStringLiteral("add_to_places")));
    }

    addSeparator();

    // Insert 'Move to Trash' and/or 'Delete'
    const bool showDeleteAction = (KSharedConfig::openConfig()->group("KDE").readEntry("ShowDeleteCommand", false) ||
                                    !properties.isLocal());
    const bool showMoveToTrashAction = (properties.isLocal() &&
                                        properties.supportsMoving());

    if (showDeleteAction && showMoveToTrashAction) {
        delete m_removeAction;
        m_removeAction = nullptr;
        addAction(m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::MoveToTrash)));
        addAction(m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::DeleteFile)));
    } else if (showDeleteAction && !showMoveToTrashAction) {
        addAction(m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::DeleteFile)));
    } else {
        if (!m_removeAction) {
            m_removeAction = new DolphinRemoveAction(this, m_mainWindow->actionCollection());
        }
        addAction(m_removeAction);
        m_removeAction->update();
    }
}

bool DolphinContextMenu::placeExists(const QUrl& url) const
{
    const KFilePlacesModel* placesModel = DolphinPlacesModelSingleton::instance().placesModel();

    const auto& matchedPlaces = placesModel->match(placesModel->index(0,0), KFilePlacesModel::UrlRole, url, 1, Qt::MatchExactly);

    return !matchedPlaces.isEmpty();
}

QAction* DolphinContextMenu::createPasteAction()
{
    QAction* action = nullptr;
    KFileItem destItem;
    if (!m_fileInfo.isNull() && m_selectedItems.count() <= 1) {
        destItem = m_fileInfo;
    } else {
        destItem = baseFileItem();
    }

    if (!destItem.isNull() && destItem.isDir()) {
        const QMimeData *mimeData = QApplication::clipboard()->mimeData();
        bool canPaste;
        const QString text = KIO::pasteActionText(mimeData, &canPaste, destItem);
        if (canPaste) {
            if (destItem == m_fileInfo) {
                // if paste destination is a selected folder
                action = new QAction(QIcon::fromTheme(QStringLiteral("edit-paste")), text, this);
                connect(action, &QAction::triggered, m_mainWindow, &DolphinMainWindow::pasteIntoFolder);
            } else {
                action = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Paste));
            }
        }
    }

    return action;
}

KFileItemListProperties& DolphinContextMenu::selectedItemsProperties() const
{
    if (!m_selectedItemsProperties) {
        m_selectedItemsProperties = new KFileItemListProperties(m_selectedItems);
    }
    return *m_selectedItemsProperties;
}

KFileItem DolphinContextMenu::baseFileItem()
{
    if (!m_baseFileItem) {
        const DolphinView* view = m_mainWindow->activeViewContainer()->view();
        KFileItem baseItem = view->rootItem();
        if (baseItem.isNull() || baseItem.url() != m_baseUrl) {
            m_baseFileItem = new KFileItem(m_baseUrl);
        } else {
            m_baseFileItem = new KFileItem(baseItem);
        }
    }
    return *m_baseFileItem;
}

void DolphinContextMenu::addOpenWithActions()
{
    // insert 'Open With...' action or sub menu
    m_fileItemActions->insertOpenWithActionsTo(nullptr, this, QStringList{qApp->desktopFileName()});
}

void DolphinContextMenu::addAdditionalActions(const KFileItemListProperties &props)
{
    addSeparator();

    QList<QAction *> additionalActions;
    if (props.isLocal() && ContextMenuSettings::showOpenTerminal()) {
        additionalActions << m_mainWindow->actionCollection()->action(QStringLiteral("open_terminal_here"));
    }
    m_fileItemActions->addActionsTo(this, KFileItemActions::MenuActionSource::All, additionalActions);

    const DolphinView* view = m_mainWindow->activeViewContainer()->view();
    const QList<QAction*> versionControlActions = view->versionControlActions(m_selectedItems);
    if (!versionControlActions.isEmpty()) {
        addActions(versionControlActions);
        addSeparator();
    }
}

