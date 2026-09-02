/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinitemlistview.h"

#include "kitemviews/kitemmodelbase.h"   // Exxos/Win7 tile grid: model signals

#include <QGraphicsScene>
#include <QGraphicsView>

#include "dolphin_compactmodesettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"
#include "dolphinfileitemlistwidget.h"
#include "settings/viewmodes/viewmodesettings.h"
#include "zoomlevelinfo.h"

#include <KIO/PreviewJob>
#include <QtMath>


DolphinItemListView::DolphinItemListView(QGraphicsWidget* parent) :
    KFileItemListView(parent),
    m_zoomLevel(0),
    m_hasCapacityItems(false)
{
    updateFont();
    updateGridSize();
}

DolphinItemListView::~DolphinItemListView()
{
    writeSettings();
}

void DolphinItemListView::setZoomLevel(int level)
{
    if (level < ZoomLevelInfo::minimumLevel()) {
        level = ZoomLevelInfo::minimumLevel();
    } else if (level > ZoomLevelInfo::maximumLevel()) {
        level = ZoomLevelInfo::maximumLevel();
    }

    if (level == m_zoomLevel) {
        return;
    }

    m_zoomLevel = level;

    ViewModeSettings settings(itemLayout());
    if (previewsShown()) {
        const int previewSize = ZoomLevelInfo::iconSizeForZoomLevel(level);
        settings.setPreviewSize(previewSize);
    } else {
        const int iconSize = ZoomLevelInfo::iconSizeForZoomLevel(level);
        settings.setIconSize(iconSize);
    }

    updateGridSize();
}

int DolphinItemListView::zoomLevel() const
{
    return m_zoomLevel;
}

void DolphinItemListView::setEnabledSelectionToggles(DolphinItemListView::SelectionTogglesEnabled selectionTogglesEnabled)
{
    m_selectionTogglesEnabled = selectionTogglesEnabled;
    switch (m_selectionTogglesEnabled) {
    case True:
        return setEnabledSelectionToggles(true);
    case False:
        return setEnabledSelectionToggles(false);
    case FollowSetting:
        return setEnabledSelectionToggles(GeneralSettings::showSelectionToggle());
    }
}

void DolphinItemListView::readSettings()
{
    // We load the settings for all view modes now because we don't load them when the view mode changes.
    IconsModeSettings::self()->load();
    CompactModeSettings::self()->load();
    DetailsModeSettings::self()->load();

    beginTransaction();

    setEnabledSelectionToggles(m_selectionTogglesEnabled);
    setHighlightEntireRow(itemLayoutHighlightEntireRow(itemLayout()));
    setSupportsItemExpanding(itemLayoutSupportsItemExpanding(itemLayout()));

    updateFont();
    updateGridSize();

    const KConfigGroup globalConfig(KSharedConfig::openConfig(), "PreviewSettings");
    setEnabledPlugins(globalConfig.readEntry("Plugins", KIO::PreviewJob::defaultPlugins()));
    setLocalFileSizePreviewLimit(globalConfig.readEntry("MaximumSize", 0));
    endTransaction();
}

void DolphinItemListView::writeSettings()
{
    IconsModeSettings::self()->save();
    CompactModeSettings::self()->save();
    DetailsModeSettings::self()->save();
}

KItemListWidgetCreatorBase* DolphinItemListView::defaultWidgetCreator() const
{
    return new KItemListWidgetCreator<DolphinFileItemListWidget>();
}

bool DolphinItemListView::itemLayoutHighlightEntireRow(ItemLayout layout) const
{
    return layout == DetailsLayout && DetailsModeSettings::highlightEntireRow();
}

bool DolphinItemListView::itemLayoutSupportsItemExpanding(ItemLayout layout) const
{
    return layout == DetailsLayout && DetailsModeSettings::expandableFolders();
}

void DolphinItemListView::onItemLayoutChanged(ItemLayout current, ItemLayout previous)
{
    setHeaderVisible(current == DetailsLayout);

    /* Exxos/Win7: a mode change is not a zoom.

       updateGridSize() below changes the icon size, and letting that animate
       meant switching to Compact grew every icon into place while the whole
       layout was being rebuilt underneath -- which is where the old drawing
       artefacts came back. Explorer switches modes instantly. */
    exxosBeginLayoutSwitch();

    updateFont();
    updateGridSize();

    KFileItemListView::onItemLayoutChanged(current, previous);

    exxosEndLayoutSwitch();
}

void DolphinItemListView::onPreviewsShownChanged(bool shown)
{
    Q_UNUSED(shown)
    exxosBeginLayoutSwitch();   // a settings change, not a zoom
    updateGridSize();
    exxosEndLayoutSwitch();
}

void DolphinItemListView::onVisibleRolesChanged(const QList<QByteArray>& current,
                                                const QList<QByteArray>& previous)
{
    KFileItemListView::onVisibleRolesChanged(current, previous);
    exxosBeginLayoutSwitch();   // a settings change, not a zoom
    updateGridSize();
    exxosEndLayoutSwitch();
}

void DolphinItemListView::updateFont()
{
    const ViewModeSettings settings(itemLayout());

    if (settings.useSystemFont()) {
        KItemListView::updateFont();
    } else {
        QFont font(settings.viewFont());

        KItemListStyleOption option = styleOption();
        option.font = font;
        option.fontMetrics = QFontMetrics(font);

        setStyleOption(option);
    }
}

/* True when any item in the current view advertises drive capacity, i.e. the
   model carries the "totalSpace" role. Only the first few items are checked --
   a location either is a drive list or is not, and this runs on every grid
   recalculation. */
bool DolphinItemListView::viewHasCapacityItems() const
{
    const KItemModelBase* m = model();
    if (!m) {
        return false;
    }
    const int probe = qMin(m->count(), 5);
    for (int i = 0; i < probe; ++i) {
        if (m->data(i).value("totalSpace").toULongLong() > 0) {
            return true;
        }
    }
    return false;
}

/* Exxos/Win7: watch the model so the tile grid can be applied once items load. */
void DolphinItemListView::onModelChanged(KItemModelBase* current, KItemModelBase* previous)
{
    KFileItemListView::onModelChanged(current, previous);
    if (previous) {
        disconnect(previous, nullptr, this, nullptr);
    }
    if (current) {
        connect(current, &KItemModelBase::itemsInserted,
                this, &DolphinItemListView::slotCapacityItemsMayHaveChanged);
        connect(current, &KItemModelBase::itemsRemoved,
                this, &DolphinItemListView::slotCapacityItemsMayHaveChanged);
    }
    slotCapacityItemsMayHaveChanged();
}

/* Only relayout when the answer actually CHANGES -- itemsInserted fires
   repeatedly while a directory loads, and updateGridSize() is not cheap. */
void DolphinItemListView::slotCapacityItemsMayHaveChanged()
{
    const bool now = viewHasCapacityItems();
    if (now != m_hasCapacityItems) {
        m_hasCapacityItems = now;
        /* Not a zoom, so it must not look like one.

           A tile grid uses a different icon size from an ordinary grid, so
           this flip changes the icon size -- and putting a disc in or taking
           one out empties and refills the model, flipping it twice. The icons
           then GREW into place exactly as if the zoom slider had been dragged,
           on every single media change. A refresh should just redraw. */
        exxosBeginLayoutSwitch();
        updateGridSize();
        exxosEndLayoutSwitch();
        return;
    }
}

void DolphinItemListView::updateGridSize()
{
    const ViewModeSettings settings(itemLayout());

    // Calculate the size of the icon
    const int iconSize = previewsShown() ? settings.previewSize() : settings.iconSize();
    m_zoomLevel = ZoomLevelInfo::zoomLevelForIconSize(QSize(iconSize, iconSize));
    KItemListStyleOption option = styleOption();

    const int padding = 2;
    int horizontalMargin = 0;
    int verticalMargin = 0;

    // Calculate the item-width and item-height
    int itemWidth;
    int itemHeight;
    int maxTextLines = 0;
    int maxTextWidth = 0;
    bool tileLayout = false;   // Exxos/Win7 tile grid
    int effectiveIconSize = iconSize;   // Exxos/Win7: a tile row enforces a floor

    switch (itemLayout()) {
    case KFileItemListView::IconsLayout: {

        // an exponential factor based on zoom, 0 -> 1, 4 -> 1.36 8 -> ~1.85, 16 -> 3.4
        auto zoomFactor = qExp(m_zoomLevel / 13.0);
        // 9 is the average char width for 10pt Noto Sans, making fontFactor =1
        // by each pixel the font gets larger the factor increases by 1/9
        auto fontFactor = option.fontMetrics.averageCharWidth() / 9.0;
        itemWidth = 48 + IconsModeSettings::textWidthIndex() * 64 * fontFactor * zoomFactor;

        if (itemWidth < iconSize + padding * 2 * zoomFactor) {
            itemWidth = iconSize + padding * 2 * zoomFactor;
        }

        itemHeight = padding * 3 + iconSize + option.fontMetrics.lineSpacing();

        /* Exxos/Win7 tile grid.
           When the current location advertises drive capacity (the computer:/
           worker does), Explorer lays the drives out as WIDE, SHORT tiles
           flowed into several columns -- icon left, name / bar / free-space
           text stacked to its right.  The cell therefore has to be wide enough
           for the icon plus a ~220px bar, and only tall enough for three lines
           of text or the icon, whichever is larger.

           This is decided per LOCATION, not globally: viewHasCapacityItems()
           inspects the model, so ordinary folders keep the normal icon grid. */
        tileLayout = viewHasCapacityItems();
        if (tileLayout) {
            /* 32px floor -- the zoom slider's own minimum.

               This was 48 when the row height still followed the ICON size, so
               a smaller icon meant a shorter row and the three stacked lines
               collided. The height is now max(icon, textBlock), so the text
               always has its own room and the icon is free to be smaller. That
               makes the 32 and 48 stops visibly different again, which they
               were not while both were clamped to 48. */
            effectiveIconSize = qMax(iconSize, 32);
            /* Draw the icon larger than the zoom level asks for. Four rows of
               text beside a 32px icon left the icon looking lost; the boost is
               divided back out inside exxosTileScale(), so the fonts and the
               bar still follow the zoom and the stops stay distinct. */
            effectiveIconSize = qRound(effectiveIconSize
                                       * KStandardItemListWidget::exxosTileIconBoost());
            /* The cell has to grow with the zoom too, or the icon gets bigger
               while the bar and the text stay the size they were and the tile
               stops looking like one thing. Same scale the widget uses for the
               tile fonts. */
            const qreal tileScale = KStandardItemListWidget::exxosTileScale(effectiveIconSize);
            const int barWidth  = qRound(220 * tileScale);
            /* Four rows -- label, drive model, bar, free space -- but two of
               them are set smaller and the bar is flatter than a line of text,
               so 3.7 lines is the room they actually need. Asking for a full
               4 left a band of dead space inside every tile. */
            const int textBlock = qRound(3.7 * option.fontMetrics.lineSpacing() * tileScale);
            // icon + the gap the widget leaves after it + the bar
            itemWidth  = padding * 2 + effectiveIconSize
                       + KStandardItemListWidget::exxosTileIconGap(effectiveIconSize)
                       + barWidth;
            itemHeight = padding * 3 + qMax(effectiveIconSize, textBlock);
            horizontalMargin = 8;
            /* Room between the rows. With one line of text a 4px gap read as a
               list; with four it read as one continuous block of text and the
               drives ran together. */
            verticalMargin = 12;
            maxTextLines = 1;
        }

        horizontalMargin = horizontalMargin ? horizontalMargin : 4;
        verticalMargin = verticalMargin ? verticalMargin : 8;
        maxTextLines = maxTextLines ? maxTextLines : IconsModeSettings::maximumTextLines();
        break;
    }
    case KFileItemListView::CompactLayout: {
        itemWidth = padding * 4 + iconSize + option.fontMetrics.height() * 5;
        const int textLinesCount = visibleRoles().count();
        itemHeight = padding * 2 + qMax(iconSize, textLinesCount * option.fontMetrics.lineSpacing());

        if (CompactModeSettings::maximumTextWidthIndex() > 0) {
            // A restriction is given for the maximum width of the text (0 means
            // having no restriction)
            maxTextWidth = option.fontMetrics.height() * 10 * CompactModeSettings::maximumTextWidthIndex();
        }

        horizontalMargin = 8;
        break;
    }
    case KFileItemListView::DetailsLayout: {
        itemWidth = -1;
        /* Exxos/Win7: a capacity row is a tile here too -- name, capacity bar
           and free-space line stacked beside the icon. An ordinary details row
           only has to fit ONE line, so its height follows the icon; below a
           48px icon that leaves the three stacked lines less room than they
           need and the free-space text rides up over the bar and the name.

           So a tile row keeps a 48px floor under the icon AND is tall enough
           for three lines, whichever is larger. Ordinary rows are untouched,
           and can still be made as small as the user likes. */
        tileLayout = viewHasCapacityItems();
        if (tileLayout) {
            effectiveIconSize = qMax(iconSize, 32);   // see the icons branch
            const qreal tileScale = KStandardItemListWidget::exxosTileScale(effectiveIconSize);
            /* FOUR rows now, not three: what the hardware is, the medium's
               own name, the capacity bar, and the free-space line. The label
               shared line one before and was the half that got clipped. */
            const int textBlock = qRound(3.7 * option.fontMetrics.lineSpacing() * tileScale);
            itemHeight = padding * 2 + qMax(effectiveIconSize, textBlock);
        } else {
            itemHeight = padding * 2 + qMax(iconSize, option.fontMetrics.lineSpacing());
        }
        break;
    }
    default:
        itemWidth = -1;
        itemHeight = -1;
        Q_ASSERT(false);
        break;
    }

    // Apply the calculated values
    option.padding = padding;
    option.horizontalMargin = horizontalMargin;
    option.verticalMargin = verticalMargin;
    option.iconSize = effectiveIconSize;
    option.maxTextLines = maxTextLines;
    option.maxTextWidth = maxTextWidth;
    option.tileLayout = tileLayout;
    beginTransaction();
    setStyleOption(option);
    setItemSize(QSizeF(itemWidth, itemHeight));
    endTransaction();

    /* Exxos/Win7: repaint the container's viewport after a grid change.

       A tile cell is much wider than a normal icon cell, so a zoom step moves
       the columns sideways by a large amount, and the strip they vacate is
       plain view background -- not inside any widget's rect. The view only
       invalidates the rects its widgets claim, so nothing ever repaints that
       strip and it keeps the left edges of the icons that used to be there.

       This is a QGraphicsWidget, so update() only schedules its own rect
       within the scene; the stale pixels are in the container's viewport, so
       that is what has to be told to repaint. Runs on zoom and layout changes
       only. */
    if (QGraphicsScene *graphicsScene = scene()) {
        const auto views = graphicsScene->views();
        for (QGraphicsView *view : views) {
            view->viewport()->update();
        }
    }
}
