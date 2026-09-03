/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "global.h"
#include "dolphinsearchbox.h"

#include "dolphin_searchsettings.h"
#include "dolphinfacetswidget.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinquery.h"

#include <KLocalizedString>
#include <KMoreToolsMenuFactory>
#include <KSeparator>
#include "config-dolphin.h"
#if HAVE_BALOO
#include <Baloo/Query>
#include <Baloo/IndexerConfig>
#endif

#include <QButtonGroup>
#include <QDir>
#include <QFontDatabase>
#include <KFilePlacesModel>
#include <QMenu>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QRegularExpression>
#include <QUrlQuery>

DolphinSearchBox::DolphinSearchBox(QWidget* parent) :
    QWidget(parent),
    m_startedSearching(false),
    m_active(true),
    m_topLayout(nullptr),
    m_searchInput(nullptr),
    m_stopButton(nullptr),
    m_searchProgress(nullptr),
    m_saveSearchAction(nullptr),
    m_historyAction(nullptr),
    m_history(),
    m_optionsScrollArea(nullptr),
    m_fileNameButton(nullptr),
    m_contentButton(nullptr),
    m_separator(nullptr),
    m_fromHereButton(nullptr),
    m_everywhereButton(nullptr),
    m_facetsWidget(nullptr),
    m_searchPath(),
    m_startSearchTimer(nullptr)
{
}

DolphinSearchBox::~DolphinSearchBox()
{
    saveSettings();
}

void DolphinSearchBox::setText(const QString& text)
{
    m_searchInput->setText(text);
}

QString DolphinSearchBox::text() const
{
    return m_searchInput->text();
}

void DolphinSearchBox::setSearchPath(const QUrl& url)
{
    if (url == m_searchPath) {
        return;
    }

    const QUrl cleanedUrl = url.adjusted(QUrl::RemoveUserInfo | QUrl::StripTrailingSlash);

    if (cleanedUrl.path() == QDir::homePath()) {
        m_fromHereButton->setChecked(false);
        m_everywhereButton->setChecked(true);
        if (!m_searchPath.isEmpty()) {
            return;
        }
    } else {
        m_everywhereButton->setChecked(false);
        m_fromHereButton->setChecked(true);
    }

    m_searchPath = url;

    QFontMetrics metrics(m_fromHereButton->font());
    const int maxWidth = metrics.height() * 8;

    QString location = cleanedUrl.fileName();
    if (location.isEmpty()) {
        location = cleanedUrl.toString(QUrl::PreferLocalFile);
    }
    const QString elidedLocation = metrics.elidedText(location, Qt::ElideMiddle, maxWidth);
    m_fromHereButton->setText(i18nc("action:button", "From Here (%1)", elidedLocation));
    m_fromHereButton->setToolTip(i18nc("action:button", "Limit search to '%1' and its subfolders", cleanedUrl.toString(QUrl::PreferLocalFile)));
}

QUrl DolphinSearchBox::searchPath() const
{
    return m_everywhereButton->isChecked() ? QUrl::fromLocalFile(QDir::homePath()) : m_searchPath;
}

QUrl DolphinSearchBox::urlForSearching() const
{
    QUrl url;

    if (isIndexingEnabled()) {
        url = balooUrlForSearching();
    } else {
        url.setScheme(QStringLiteral("filenamesearch"));

        QUrlQuery query;

        // The filenamesearch worker matches a plain substring, so a pattern
        // like *.mid finds nothing at all - no file has an asterisk in its
        // name. Send it the longest literal run instead, which returns a
        // superset, and carry the pattern itself so the view can apply proper
        // wildcard matching to what comes back.
        const QString text = m_searchInput->text();
        QString workerText = text;
        if (text.contains(QLatin1Char('*')) || text.contains(QLatin1Char('?'))
            || text.contains(QLatin1Char('['))) {
            workerText.clear();
            const QStringList literals =
                text.split(QRegularExpression(QStringLiteral("[*?\\[\\]]")), Qt::SkipEmptyParts);
            for (const QString& literal : literals) {
                if (literal.length() > workerText.length()) {
                    workerText = literal;
                }
            }
            query.addQueryItem(QStringLiteral("exxosPattern"), text);
        }

        query.addQueryItem(QStringLiteral("search"), workerText);
        if (m_contentButton->isChecked()) {
            query.addQueryItem(QStringLiteral("checkContent"), QStringLiteral("yes"));
        }

        query.addQueryItem(QStringLiteral("url"), searchPath().url());
        query.addQueryItem(QStringLiteral("title"), queryTitle(m_searchInput->text()));

        url.setQuery(query);
    }

    return url;
}

void DolphinSearchBox::fromSearchUrl(const QUrl& url)
{
    if (DolphinQuery::supportsScheme(url.scheme())) {
        const DolphinQuery query = DolphinQuery::fromSearchUrl(url);
        updateFromQuery(query);
    } else if (url.scheme() == QLatin1String("filenamesearch")) {
        const QUrlQuery query(url);
        // Show what was actually typed. The "search" item holds the wildcard-free
        // text sent to the worker, so using it here would quietly rewrite *.mid
        // as .mid the moment the URL is read back.
        const QString pattern = query.queryItemValue(QStringLiteral("exxosPattern"));
        setText(pattern.isEmpty() ? query.queryItemValue(QStringLiteral("search")) : pattern);
        if (m_searchPath.scheme() != url.scheme()) {
            m_searchPath = QUrl();
        }
        setSearchPath(QUrl::fromUserInput(query.queryItemValue(QStringLiteral("url")), QString(), QUrl::AssumeLocalFile));
        m_contentButton->setChecked(query.queryItemValue(QStringLiteral("checkContent")) == QLatin1String("yes"));
    } else {
        setText(QString());
        m_searchPath = QUrl();
        setSearchPath(url);
    }

    updateFacetsVisible();
}

void DolphinSearchBox::selectAll()
{
    m_searchInput->selectAll();
}

void DolphinSearchBox::setActive(bool active)
{
    if (active != m_active) {
        m_active = active;

        if (active) {
            Q_EMIT activated();
        }
    }
}

bool DolphinSearchBox::isActive() const
{
    return m_active;
}

bool DolphinSearchBox::event(QEvent* event)
{
    if (event->type() == QEvent::Polish) {
        init();
    }
    return QWidget::event(event);
}

void DolphinSearchBox::showEvent(QShowEvent* event)
{
    if (!event->spontaneous()) {
        m_searchInput->setFocus();
        m_startedSearching = false;
    }
}

void DolphinSearchBox::hideEvent(QHideEvent* event)
{
    Q_UNUSED(event)
    m_startedSearching = false;
    m_startSearchTimer->stop();
}

void DolphinSearchBox::keyReleaseEvent(QKeyEvent* event)
{
    QWidget::keyReleaseEvent(event);
    if (event->key() == Qt::Key_Escape) {
        if (m_searchInput->text().isEmpty()) {
            Q_EMIT closeRequest();
        } else {
            m_searchInput->clear();
        }
    }
    else if (event->key() == Qt::Key_Down) {
        Q_EMIT focusViewRequest();
    }
}

bool DolphinSearchBox::eventFilter(QObject* obj, QEvent* event)
{
    switch (event->type()) {
    case QEvent::FocusIn:
        // #379135: we get the FocusIn event when we close a tab but we don't want to emit
        // the activated() signal before the removeTab() call in DolphinTabWidget::closeTab() returns.
        // To avoid this issue, we delay the activation of the search box.
        // We also don't want to schedule the activation process if we are already active,
        // otherwise we can enter in a loop of FocusIn/FocusOut events with the searchbox of another tab.
        if (!isActive()) {
            QTimer::singleShot(0, this, [this] {
                setActive(true);
                setFocus();
            });
        }
        break;

    default:
        break;
    }

    return QObject::eventFilter(obj, event);
}

void DolphinSearchBox::rememberSearch(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    m_history.removeAll(text);
    m_history.prepend(text);
    while (m_history.count() > 20) {
        m_history.removeLast();
    }
    KConfigGroup group(KSharedConfig::openConfig(), "Search");
    group.writeEntry("History", m_history);
    group.sync();
}

void DolphinSearchBox::showHistoryMenu()
{
    QMenu menu(this);

    // Saved searches first - they were deliberately kept, so they outrank
    // whatever happens to have been typed lately. The floppy button beside
    // the field puts them in Places, which is where they are read back from.
    const QIcon savedIcon = QIcon::fromTheme(QStringLiteral("folder-saved-search-symbolic"));
    KFilePlacesModel* places = DolphinPlacesModelSingleton::instance().placesModel();
    int savedCount = 0;
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        const QUrl url = places->url(index);
        if (url.scheme() != QLatin1String("filenamesearch")
            && url.scheme() != QLatin1String("baloosearch")) {
            continue;
        }
        QAction* action = menu.addAction(savedIcon, places->text(index));
        connect(action, &QAction::triggered, this, [this, url]() {
            fromSearchUrl(url);
            emitSearchRequest();
        });
        ++savedCount;
    }

    if (savedCount > 0 && !m_history.isEmpty()) {
        menu.addSeparator();
    }

    if (savedCount == 0 && m_history.isEmpty()) {
        return;
    }

    for (const QString& entry : qAsConst(m_history)) {
        QAction* action = menu.addAction(entry);
        connect(action, &QAction::triggered, this, [this, entry]() {
            setText(entry);
            emitSearchRequest();
        });
    }
    menu.exec(m_searchInput->mapToGlobal(QPoint(0, m_searchInput->height())));
}

void DolphinSearchBox::emitSearchRequest()
{
    rememberSearch(m_searchInput->text());
    m_startSearchTimer->stop();
    m_startedSearching = true;
    m_saveSearchAction->setEnabled(true);
    Q_EMIT searchRequest();
}

void DolphinSearchBox::setSearchRunning(bool running)
{
    if (m_stopButton) {
        m_stopButton->setVisible(running);
    }
    if (m_searchProgress) {
        m_searchProgress->setVisible(running);
    }
}

void DolphinSearchBox::emitCloseRequest()
{
    m_startSearchTimer->stop();
    m_startedSearching = false;
    m_saveSearchAction->setEnabled(false);
    Q_EMIT closeRequest();
}

void DolphinSearchBox::slotConfigurationChanged()
{
    saveSettings();
    if (m_startedSearching) {
        emitSearchRequest();
    }
}

void DolphinSearchBox::slotSearchTextChanged(const QString& text)
{

    if (text.isEmpty()) {
        m_startSearchTimer->stop();
    } else {
        m_startSearchTimer->start();
    }
    Q_EMIT searchTextChanged(text);
}

void DolphinSearchBox::slotReturnPressed()
{
    emitSearchRequest();
    Q_EMIT focusViewRequest();
}

void DolphinSearchBox::slotFacetChanged()
{
    m_startedSearching = true;
    m_startSearchTimer->stop();
    Q_EMIT searchRequest();
}

void DolphinSearchBox::slotSearchSaved()
{
    const QUrl searchURL = urlForSearching();
    if (searchURL.isValid()) {
        const QString label = i18n("Search for %1 in %2", text(), searchPath().fileName());
        DolphinPlacesModelSingleton::instance().placesModel()->addPlace(label, searchURL, QStringLiteral("folder-saved-search-symbolic"));
    }
}

void DolphinSearchBox::initButton(QToolButton* button)
{
    button->installEventFilter(this);
    button->setAutoExclusive(true);
    button->setAutoRaise(true);
    button->setCheckable(true);
    connect(button, &QToolButton::clicked, this, &DolphinSearchBox::slotConfigurationChanged);
}

void DolphinSearchBox::loadSettings()
{
    if (SearchSettings::location() == QLatin1String("Everywhere")) {
        m_everywhereButton->setChecked(true);
    } else {
        m_fromHereButton->setChecked(true);
    }

    if (SearchSettings::what() == QLatin1String("Content")) {
        m_contentButton->setChecked(true);
    } else {
        m_fileNameButton->setChecked(true);
    }

    updateFacetsVisible();
}

void DolphinSearchBox::saveSettings()
{
    SearchSettings::setLocation(m_fromHereButton->isChecked() ? QStringLiteral("FromHere") : QStringLiteral("Everywhere"));
    SearchSettings::setWhat(m_fileNameButton->isChecked() ? QStringLiteral("FileName") : QStringLiteral("Content"));
    SearchSettings::self()->save();
}

void DolphinSearchBox::init()
{
    // Create search box
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(i18n("Search..."));
    m_searchInput->installEventFilter(this);
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    connect(m_searchInput, &QLineEdit::returnPressed,
            this, &DolphinSearchBox::slotReturnPressed);
    connect(m_searchInput, &QLineEdit::textChanged,
            this, &DolphinSearchBox::slotSearchTextChanged);
    setFocusProxy(m_searchInput);

    // Add "Save search" button inside search box
    m_saveSearchAction = new QAction(this);
    m_saveSearchAction->setIcon (QIcon::fromTheme(QStringLiteral("document-save-symbolic")));
    m_saveSearchAction->setText(i18nc("action:button", "Save this search to quickly access it again in the future"));
    m_saveSearchAction->setEnabled(false);
    m_searchInput->addAction(m_saveSearchAction, QLineEdit::TrailingPosition);

    // Windows keeps the last searches a click away rather than making you
    // retype them. The list lives in dolphinrc, so it survives a restart.
    m_history = KConfigGroup(KSharedConfig::openConfig(), "Search").readEntry("History", QStringList());
    m_historyAction = new QAction(this);
    m_historyAction->setIcon(QIcon::fromTheme(QStringLiteral("go-down-search")));
    m_historyAction->setText(i18nc("@action:inmenu", "Recent Searches"));
    m_historyAction->setToolTip(i18nc("@info:tooltip", "Show the last 20 searches"));
    connect(m_historyAction, &QAction::triggered, this, &DolphinSearchBox::showHistoryMenu);
    m_searchInput->addAction(m_historyAction, QLineEdit::TrailingPosition);
    connect(m_saveSearchAction, &QAction::triggered, this, &DolphinSearchBox::slotSearchSaved);

    // Create close button
    QToolButton* closeButton = new QToolButton(this);
    closeButton->setAutoRaise(true);
    closeButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
    closeButton->setToolTip(i18nc("@info:tooltip", "Quit searching"));
    connect(closeButton, &QToolButton::clicked, this, &DolphinSearchBox::emitCloseRequest);

    // Stopping a search is not the same as leaving it: the results found so
    // far stay where they are. Without a button for it the only way to stop
    // was Escape, which closes the search and throws the results away.
    m_stopButton = new QToolButton(this);
    m_stopButton->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stopButton->setText(i18nc("action:button", "Stop"));
    m_stopButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_stopButton->setIconSize(QSize(22, 22));
    m_stopButton->setToolTip(i18nc("@info:tooltip", "Stop searching, keeping the results found so far"));
    m_stopButton->hide();

    // A search over a big tree takes a while and Dolphin's own progress lives
    // in the status bar, at the far corner of the window from where you are
    // looking. Put an indicator on the search bar itself, where the search is.
    m_searchProgress = new QProgressBar(this);
    m_searchProgress->setRange(0, 0);            // busy indicator, not a percentage
    m_searchProgress->setTextVisible(false);
    m_searchProgress->setFixedWidth(110);
    m_searchProgress->setMaximumHeight(m_stopButton->sizeHint().height() - 6);
    m_searchProgress->hide();
    connect(m_stopButton, &QToolButton::clicked, this, [this]() {
        Q_EMIT stopSearchRequest();
    });

    // Apply layout for the search input
    QHBoxLayout* searchInputLayout = new QHBoxLayout();
    searchInputLayout->setContentsMargins(0, 0, 0, 0);
    searchInputLayout->addWidget(m_searchInput);
    searchInputLayout->addWidget(closeButton);

    // Create "Filename" and "Content" button
    m_fileNameButton = new QToolButton(this);
    m_fileNameButton->setText(i18nc("action:button", "Filename"));
    initButton(m_fileNameButton);

    m_contentButton = new QToolButton();
    m_contentButton->setText(i18nc("action:button", "Content"));
    initButton(m_contentButton);

    QButtonGroup* searchWhatGroup = new QButtonGroup(this);
    searchWhatGroup->addButton(m_fileNameButton);
    searchWhatGroup->addButton(m_contentButton);

    m_separator = new KSeparator(Qt::Vertical, this);

    // Create "From Here" and "Your files" buttons
    m_fromHereButton = new QToolButton(this);
    m_fromHereButton->setText(i18nc("action:button", "From Here"));
    initButton(m_fromHereButton);

    m_everywhereButton = new QToolButton(this);
    m_everywhereButton->setText(i18nc("action:button", "Your files"));
    m_everywhereButton->setToolTip(i18nc("action:button", "Search in your home directory"));
    m_everywhereButton->setIcon(QIcon::fromTheme(QStringLiteral("user-home")));
    m_everywhereButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    initButton(m_everywhereButton);

    QButtonGroup* searchLocationGroup = new QButtonGroup(this);
    searchLocationGroup->addButton(m_fromHereButton);
    searchLocationGroup->addButton(m_everywhereButton);

    auto moreSearchToolsButton = new QToolButton(this);
    moreSearchToolsButton->setAutoRaise(true);
    moreSearchToolsButton->setPopupMode(QToolButton::InstantPopup);
    moreSearchToolsButton->setIcon(QIcon::fromTheme("arrow-down-double"));
    moreSearchToolsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    moreSearchToolsButton->setText(i18n("More Search Tools"));
    moreSearchToolsButton->setMenu(new QMenu(this));
    connect(moreSearchToolsButton->menu(), &QMenu::aboutToShow, moreSearchToolsButton->menu(), [this, moreSearchToolsButton]()
    {
        m_menuFactory.reset(new KMoreToolsMenuFactory("dolphin/search-tools"));
        moreSearchToolsButton->menu()->clear();
        m_menuFactory->fillMenuFromGroupingNames(moreSearchToolsButton->menu(), { "files-find" }, this->m_searchPath);
    } );

    // Create "Facets" widget
    m_facetsWidget = new DolphinFacetsWidget(this);
    m_facetsWidget->installEventFilter(this);
    m_facetsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_facetsWidget->layout()->setSpacing(Dolphin::LAYOUT_SPACING_SMALL);
    connect(m_facetsWidget, &DolphinFacetsWidget::facetChanged, this, &DolphinSearchBox::slotFacetChanged);

    // Put the options into a QScrollArea. This prevents increasing the view width
    // in case that not enough width for the options is available.
    QWidget* optionsContainer = new QWidget(this);

    // Apply layout for the options
    QHBoxLayout* optionsLayout = new QHBoxLayout(optionsContainer);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(Dolphin::LAYOUT_SPACING_SMALL);
    optionsLayout->addWidget(m_fileNameButton);
    optionsLayout->addWidget(m_contentButton);
    optionsLayout->addWidget(m_separator);
    optionsLayout->addWidget(m_fromHereButton);
    optionsLayout->addWidget(m_everywhereButton);
    optionsLayout->addWidget(new KSeparator(Qt::Vertical, this));
    optionsLayout->addWidget(moreSearchToolsButton);
    optionsLayout->addStretch(1);
    optionsLayout->addWidget(m_stopButton);
    optionsLayout->addWidget(m_searchProgress);

    m_optionsScrollArea = new QScrollArea(this);
    m_optionsScrollArea->setFrameShape(QFrame::NoFrame);
    m_optionsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_optionsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_optionsScrollArea->setMaximumHeight(optionsContainer->sizeHint().height());
    m_optionsScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_optionsScrollArea->setWidget(optionsContainer);
    m_optionsScrollArea->setWidgetResizable(true);

    m_topLayout = new QVBoxLayout(this);
    m_topLayout->setContentsMargins(0, Dolphin::LAYOUT_SPACING_SMALL, 0, 0);
    m_topLayout->setSpacing(Dolphin::LAYOUT_SPACING_SMALL);
    m_topLayout->addLayout(searchInputLayout);
    m_topLayout->addWidget(m_optionsScrollArea);
    m_topLayout->addWidget(m_facetsWidget);

    loadSettings();

    // The searching should be started automatically after the user did not change
    // the text within one second
    m_startSearchTimer = new QTimer(this);
    m_startSearchTimer->setSingleShot(true);
    m_startSearchTimer->setInterval(1000);
    connect(m_startSearchTimer, &QTimer::timeout, this, &DolphinSearchBox::emitSearchRequest);
}

QString DolphinSearchBox::queryTitle(const QString& text) const
{
    return i18nc("@title UDS_DISPLAY_NAME for a KIO directory listing. %1 is the query the user entered.",
                             "Query Results from '%1'", text);
}

QUrl DolphinSearchBox::balooUrlForSearching() const
{
#if HAVE_BALOO
    const QString text = m_searchInput->text();

    Baloo::Query query;
    query.addType(m_facetsWidget->facetType());

    QStringList queryStrings = m_facetsWidget->searchTerms();

    if (m_contentButton->isChecked()) {
        queryStrings << text;
    } else if (!text.isEmpty()) {
        queryStrings << QStringLiteral("filename:\"%1\"").arg(text);
    }

    if (m_fromHereButton->isChecked()) {
        query.setIncludeFolder(m_searchPath.toLocalFile());
    }

    query.setSearchString(queryStrings.join(QLatin1Char(' ')));

    return query.toSearchUrl(queryTitle(text));
#else
    return QUrl();
#endif
}

void DolphinSearchBox::updateFromQuery(const DolphinQuery& query)
{
    // Block all signals to avoid unnecessary "searchRequest" signals
    // while we adjust the search text and the facet widget.
    blockSignals(true);

    const QString customDir = query.includeFolder();
    if (!customDir.isEmpty()) {
        setSearchPath(QUrl::fromLocalFile(customDir));
    } else {
        setSearchPath(QUrl::fromLocalFile(QDir::homePath()));
    }

    // If the input box has focus, do not update to avoid messing with user typing
    if (!m_searchInput->hasFocus()) {
        setText(query.text());
    }

    if (query.hasContentSearch()) {
        m_contentButton->setChecked(true);
    } else if (query.hasFileName())  {
        m_fileNameButton->setChecked(true);
    }

    m_facetsWidget->resetSearchTerms();
    m_facetsWidget->setFacetType(query.type());
    const QStringList searchTerms = query.searchTerms();
    for (const QString& searchTerm : searchTerms) {
        m_facetsWidget->setSearchTerm(searchTerm);
    }

    m_startSearchTimer->stop();
    blockSignals(false);
}

void DolphinSearchBox::updateFacetsVisible()
{
    const bool indexingEnabled = isIndexingEnabled();
    m_facetsWidget->setEnabled(indexingEnabled);
    m_facetsWidget->setVisible(indexingEnabled);
}

bool DolphinSearchBox::isIndexingEnabled() const
{
#if HAVE_BALOO
    const Baloo::IndexerConfig searchInfo;
    return searchInfo.fileIndexingEnabled() && !searchPath().isEmpty() && searchInfo.shouldBeIndexed(searchPath().toLocalFile());
#else
    return false;
#endif
}
