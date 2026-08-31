/*
 * SPDX-FileCopyrightText: 2006 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2006 Stefan Monov <logixoul@gmail.com>
 * SPDX-FileCopyrightText: 2015 Mathieu Tarral <mathieu.tarral@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dbusinterface.h"
#include "dolphin_generalsettings.h"
#include "dolphin_version.h"
#include "dolphindebug.h"
#include "dolphinmainwindow.h"
#include "global.h"
#include "config-dolphin.h"
#if HAVE_KUSERFEEDBACK
#include "userfeedback/dolphinfeedbackprovider.h"
#endif

#include <KApplicationTrader>
#include <KMessageBox>
#include <KService>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QTimer>
#include <QStandardPaths>
#include <KAboutData>

/* Version of the Exxos Edition patch set itself, independent of the Dolphin
   base version it is applied to. Bump this when the patch changes; leave it
   alone when merely rebasing onto a newer Dolphin. */
#define EXXOS_EDITION_VERSION "1.0"
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>
#include <KConfigGui>
#include <KIO/PreviewJob>
#include <KWindowSystem>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <Kdelibs4ConfigMigrator>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSessionManager>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include <iostream>


/* ---------------------------------------------------------------------------
   Exxos Edition: offer to become the default file manager.

   The package installs alongside the stock Dolphin rather than replacing it
   (see DOLPHIN-PATCHES.md section 12), so opening a folder from another
   application still goes to whichever file manager is registered for
   inode/directory. This asks once whether to change that, and remembers a
   "no" so it never nags.

   Nothing is changed without the user agreeing: the only write is to
   ~/.config/mimeapps.list, which is per-user and reversible from
   System Settings -> Applications -> File Associations.
   --------------------------------------------------------------------------- */
static void exxosOfferToBecomeDefaultFileManager(QWidget *parent)
{
    const QString ourDesktop = QStringLiteral("dolphin-exxos.desktop");

    // Only meaningful once the package is installed; running straight from the
    // build tree there is no .desktop file to register.
    if (QStandardPaths::locate(QStandardPaths::ApplicationsLocation, ourDesktop).isEmpty()) {
        return;
    }

    const KService::Ptr current = KApplicationTrader::preferredService(QStringLiteral("inode/directory"));
    if (current && current->desktopEntryName() == QLatin1String("dolphin-exxos")) {
        return;   // already the default
    }

    const QString currentName = current ? current->name() : i18n("(none)");
    const int answer = KMessageBox::questionYesNo(
        parent,
        i18n("<para>Folders currently open in <application>%1</application>.</para>"
             "<para>Make <application>Dolphin Exxos Edition</application> the default "
             "file manager instead?</para>", currentName),
        i18nc("@title:window", "Default File Manager"),
        KGuiItem(i18nc("@action:button", "Make Default"), QStringLiteral("dialog-ok")),
        KGuiItem(i18nc("@action:button", "Keep %1", currentName), QStringLiteral("dialog-cancel")),
        QStringLiteral("exxosAskDefaultFileManager"));   // remembers "no"

    if (answer != KMessageBox::Yes) {
        return;
    }

    KSharedConfig::Ptr mimeApps = KSharedConfig::openConfig(QStringLiteral("mimeapps.list"),
                                                           KConfig::NoGlobals,
                                                           QStandardPaths::GenericConfigLocation);
    KConfigGroup defaults(mimeApps, "Default Applications");
    KConfigGroup added(mimeApps, "Added Associations");
    // all three are used by different applications to mean "a folder"
    for (const QString &mime : { QStringLiteral("inode/directory"),
                                 QStringLiteral("x-directory/normal"),
                                 QStringLiteral("application/x-directory") }) {
        defaults.writeXdgListEntry(mime, QStringList{ ourDesktop });
        QStringList assoc = added.readXdgListEntry(mime);
        assoc.removeAll(ourDesktop);
        assoc.prepend(ourDesktop);
        added.writeXdgListEntry(mime, assoc);
    }
    mimeApps->sync();
}

int main(int argc, char **argv)
{
#ifndef Q_OS_WIN
    // Prohibit using sudo or kdesu (but allow using the root user directly)
    if (getuid() == 0) {
        if (!qEnvironmentVariableIsEmpty("SUDO_USER")) {
            std::cout << "Running Dolphin with sudo can cause bugs and expose you to security vulnerabilities. "
                         "Instead use Dolphin normally and you will be prompted for elevated privileges when "
                         "performing file operations that require them."
                      << std::endl;
            return EXIT_FAILURE;
        } else if (!qEnvironmentVariableIsEmpty("KDESU_USER")) {
            std::cout << "Running Dolphin with kdesu can cause bugs and expose you to security vulnerabilities. "
                         "Instead use Dolphin normally and you will be prompted for elevated privileges when "
                         "performing file operations that require them."
                      << std::endl;
            return EXIT_FAILURE;
        }
    }
#endif

    /**
     * enable high dpi support
     */
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("system-file-manager"), app.windowIcon()));

    KIO::PreviewJob::setDefaultDevicePixelRatio(app.devicePixelRatio());

    KCrash::initialize();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    Kdelibs4ConfigMigrator migrate(QStringLiteral("dolphin"));
    migrate.setConfigFiles(QStringList() << QStringLiteral("dolphinrc"));
    migrate.setUiFiles(QStringList() << QStringLiteral("dolphinpart.rc") << QStringLiteral("dolphinui.rc"));
    migrate.migrate();
#endif

    KLocalizedString::setApplicationDomain("dolphin");

    /* Exxos Edition branding.
       The base Dolphin version is NOT hardcoded -- DOLPHIN_VERSION_STRING is
       generated by ecm_setup_version() from the top-level CMakeLists, so after
       rebasing onto a new Dolphin tag the name and the About box both report
       the new base automatically, with no edit here. */
    const QString exxosName = i18n("Dolphin Exxos Edition (%1)",
                                   QStringLiteral(DOLPHIN_VERSION_STRING));
    const QString exxosVersion = QStringLiteral(EXXOS_EDITION_VERSION
                                                " \u00b7 base Dolphin " DOLPHIN_VERSION_STRING);

    KAboutData aboutData(QStringLiteral("dolphin"), exxosName, exxosVersion,
                         i18nc("@title", "File Manager"),
                         KAboutLicense::GPL,
                         i18nc("@info:credit", "(C) 2006-2022 The Dolphin Developers"));
    aboutData.addCredit(i18nc("@info:credit", "Exxos Edition"),
                        i18nc("@info:credit",
                              "Windows 7 style tile view with graphical capacity bars"));
    aboutData.setHomepage(QStringLiteral("https://kde.org/applications/system/org.kde.dolphin"));
    aboutData.addAuthor(i18nc("@info:credit", "Felix Ernst"),
                        i18nc("@info:credit", "Maintainer (since 2021) and developer"),
                        QStringLiteral("felixernst@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Méven Car"),
                        i18nc("@info:credit", "Maintainer (since 2021) and developer (since 2019)"),
                        QStringLiteral("meven@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Elvis Angelaccio"),
                        i18nc("@info:credit", "Maintainer (2018-2021) and developer"),
                        QStringLiteral("elvis.angelaccio@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Emmanuel Pescosta"),
                        i18nc("@info:credit", "Maintainer (2014-2018) and developer"),
                        QStringLiteral("emmanuelpescosta099@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Frank Reininghaus"),
                        i18nc("@info:credit", "Maintainer (2012-2014) and developer"),
                        QStringLiteral("frank78ac@googlemail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Peter Penz"),
                        i18nc("@info:credit", "Maintainer and developer (2006-2012)"),
                        QStringLiteral("peter.penz19@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Sebastian Trüg"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("trueg@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "David Faure"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("faure@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Aaron J. Seigo"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("aseigo@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Rafael Fernández López"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("ereslibre@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Kevin Ottens"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("ervin@kde.org"));
    aboutData.addAuthor(i18nc("@info:credit", "Holger Freyther"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("freyther@gmx.net"));
    aboutData.addAuthor(i18nc("@info:credit", "Max Blazejak"),
                        i18nc("@info:credit", "Developer"),
                        QStringLiteral("m43ksrocks@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Michael Austin"),
                        i18nc("@info:credit", "Documentation"),
                        QStringLiteral("tuxedup@users.sourceforge.net"));

    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    // command line options
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("select"), i18nc("@info:shell", "The files and folders passed as arguments "
                                                                                        "will be selected.")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("split"), i18nc("@info:shell", "Dolphin will get started with a split view.")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("new-window"), i18nc("@info:shell", "Dolphin will explicitly open in a new window.")));
    parser.addOption(QCommandLineOption(QStringList() << QStringLiteral("daemon"), i18nc("@info:shell", "Start Dolphin Daemon (only required for DBus Interface)")));
    parser.addPositionalArgument(QStringLiteral("+[Url]"), i18nc("@info:shell", "Document to open"));

    parser.process(app);
    aboutData.processCommandLine(&parser);

    const bool splitView = parser.isSet(QStringLiteral("split")) || GeneralSettings::splitView();
    const bool openFiles = parser.isSet(QStringLiteral("select"));
    const QStringList args = parser.positionalArguments();
    QList<QUrl> urls = Dolphin::validateUris(args);
    // We later mutate urls, so we need to store if it was empty originally
    const bool startedWithURLs = !urls.isEmpty();


    if (parser.isSet(QStringLiteral("daemon"))) {
        // Disable session management for the daemonized version
        // See https://bugs.kde.org/show_bug.cgi?id=417219
        auto disableSessionManagement = [](QSessionManager &sm) {
            sm.setRestartHint(QSessionManager::RestartNever);
        };
        QObject::connect(&app, &QGuiApplication::commitDataRequest, disableSessionManagement);
        QObject::connect(&app, &QGuiApplication::saveStateRequest, disableSessionManagement);

#ifdef FLATPAK
        KDBusService dolphinDBusService(KDBusService::NoExitOnFailure);
#else
        KDBusService dolphinDBusService;
#endif
        DBusInterface interface;
        interface.setAsDaemon();
        return app.exec();
    }

    if (!parser.isSet(QStringLiteral("new-window"))) {

        QString token;
        if (KWindowSystem::isPlatformWayland()) {
            token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
            qunsetenv("XDG_ACTIVATION_TOKEN");
        }

        if (Dolphin::attachToExistingInstance(urls, openFiles, splitView, QString(), token)) {
            // Successfully attached to existing instance of Dolphin
            return 0;
        }
    }

    if (!startedWithURLs) {
        // We need at least one URL to open Dolphin
        urls.append(Dolphin::homeUrl());
    }

    if (splitView && urls.size() < 2) {
        // Split view does only make sense if we have at least 2 URLs
        urls.append(urls.last());
    }

    DolphinMainWindow* mainWindow = new DolphinMainWindow();

    if (openFiles) {
        mainWindow->openFiles(urls, splitView);
    } else {
        mainWindow->openDirectories(urls, splitView);
    }

    mainWindow->show();

    // Allow starting Dolphin on a system that is not running DBus:
    KDBusService::StartupOptions serviceOptions = KDBusService::Multiple;
    if (!QDBusConnection::sessionBus().isConnected()) {
        serviceOptions |= KDBusService::NoExitOnFailure;
    }
    KDBusService dolphinDBusService(serviceOptions);
    DBusInterface interface;

    if (!app.isSessionRestored()) {
        KConfigGui::setSessionConfig(QStringLiteral("dolphin"), QStringLiteral("dolphin"));
    }

    // Only restore session if:
    // 1. Not explicitly opening a new instance
    // 2. The "remember state" setting is enabled or session restoration after
    //    reboot is in use
    // 3. There is a session available to restore
    if (!parser.isSet(QStringLiteral("new-window"))
        && (app.isSessionRestored() || GeneralSettings::rememberOpenedTabs())
    ) {
        // Get saved state data for the last-closed Dolphin instance
        const QString serviceName = QStringLiteral("org.kde.dolphin-%1").arg(QCoreApplication::applicationPid());
        if (Dolphin::dolphinGuiInstances(serviceName).size() > 0) {
            const QString className = KXmlGuiWindow::classNameOfToplevel(1);
            if (className == QLatin1String("DolphinMainWindow")) {
                mainWindow->restore(1);
                // If the user passed any URLs to Dolphin, open those in the
                // window after session-restoring it
                if (startedWithURLs) {
                    if (openFiles) {
                        mainWindow->openFiles(urls, splitView);
                    } else {
                        mainWindow->openDirectories(urls, splitView);
                    }
                }
            } else {
                qCWarning(DolphinDebug) << "Unknown class " << className << " in session saved data!";
            }
        }
    }

#if HAVE_KUSERFEEDBACK
    auto feedbackProvider = DolphinFeedbackProvider::instance();
    Q_UNUSED(feedbackProvider)
#endif

    /* Exxos: ask once, after the window is up, whether to become the default
       file manager. Deferred with a zero timer so the dialog appears over the
       main window rather than blocking before the event loop starts. */
    QTimer::singleShot(0, mainWindow, [mainWindow]() {
        exxosOfferToBecomeDefaultFileManager(mainWindow);
    });

    return app.exec(); // krazy:exclude=crash;
}
