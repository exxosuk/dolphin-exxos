/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KSTANDARDITEMLISTWIDGET_H
#define KSTANDARDITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kitemlistwidget.h"

#include <QPixmap>
#include <QPointF>
#include <QStaticText>

class KItemListRoleEditor;
class KItemListStyleOption;
class KItemListView;

class DOLPHIN_EXPORT KStandardItemListWidgetInformant : public KItemListWidgetInformant
{
public:
    KStandardItemListWidgetInformant();
    ~KStandardItemListWidgetInformant() override;
    
    void calculateItemSizeHints(QVector<std::pair<qreal /* height */, bool /* isElided */>>& logicalHeightHints, qreal& logicalWidthHint, const KItemListView* view) const override;

    qreal preferredRoleColumnWidth(const QByteArray& role,
                                           int index,
                                           const KItemListView* view) const override;

protected:
    /**
     * @return The value of the "text" role. The default implementation returns
     *         view->model()->data(index)["text"]. If a derived class can
     *         prevent the (possibly expensive) construction of the
     *         QHash<QByteArray, QVariant> returned by KItemModelBase::data(int),
     *         it can reimplement this function.
     */
    virtual QString itemText(int index, const KItemListView* view) const;

    /**
     * @return The value of the "isLink" role. The default implementation returns false.
     *         The derived class should reimplement this function, when information about
     *         links is available and in usage.
     */
    virtual bool itemIsLink(int index, const KItemListView* view) const;

    /**
     * @return String representation of the role \a role. The representation of
     *         a role might depend on other roles, so the values of all roles
     *         are passed as parameter.
     */
    virtual QString roleText(const QByteArray& role,
                             const QHash<QByteArray, QVariant>& values) const;

    /**
    * @return A font based on baseFont which is customized for symlinks.
    */
    virtual QFont customizedFontForLinks(const QFont& baseFont) const;

    void calculateIconsLayoutItemSizeHints(QVector<std::pair<qreal, bool>>& logicalHeightHints, qreal& logicalWidthHint, const KItemListView* view) const;
    void calculateCompactLayoutItemSizeHints(QVector<std::pair<qreal, bool>>& logicalHeightHints, qreal& logicalWidthHint, const KItemListView* view) const;
    void calculateDetailsLayoutItemSizeHints(QVector<std::pair<qreal, bool>>& logicalHeightHints, qreal& logicalWidthHint, const KItemListView* view) const;

    friend class KStandardItemListWidget; // Accesses roleText()
};

/**
 * @brief ItemList widget implementation for KStandardItemListView and KStandardItemModel.
 */
class DOLPHIN_EXPORT KStandardItemListWidget : public KItemListWidget
{
    Q_OBJECT

public:
    enum Layout
    {
        IconsLayout,
        CompactLayout,
        DetailsLayout
    };

    KStandardItemListWidget(KItemListWidgetInformant* informant, QGraphicsItem* parent);
    ~KStandardItemListWidget() override;

    void setLayout(Layout layout);
    Layout layout() const;

    void setHighlightEntireRow(bool highlightEntireRow);
    bool highlightEntireRow() const;

    void setSupportsItemExpanding(bool supportsItemExpanding);
    bool supportsItemExpanding() const;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    QRectF iconRect() const override;
    QRectF textRect() const override;
    QRectF textFocusRect() const override;
    QRectF selectionRect() const override;
    QRectF expansionToggleRect() const override;
    QRectF selectionToggleRect() const override;
    QPixmap createDragPixmap(const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    static KItemListWidgetInformant* createInformant();

public:
    /* Exxos/Win7: how much bigger a tile is than at the 48px base size.
       The WHOLE tile scales with the zoom -- icon, name, capacity bar and the
       free-space line -- rather than only the icon growing while the text and
       the bar stay the size they were. */
    /**
     * Exxos/Win7: how much bigger a tile's icon is drawn than the zoom level.
     *
     * A capacity tile stacks four rows of text beside the icon, so an icon
     * sized purely by the zoom looked lost next to them. The boost applies to
     * the ICON only -- exxosTileScale() divides it back out so the fonts and
     * the bar keep following the zoom, not the boost.
     */
    static qreal exxosTileIconBoost() { return 1.6; }
    static qreal exxosTileScale(int iconSize);

    /* Gap between the icon and the text column in a tile. Explorer leaves a
       clear channel there; 2*padding (4px) is not enough and at higher zoom
       the name and the capacity bar end up touching the icon. Shared with
       DolphinItemListView::updateGridSize() so the cell is wide enough for
       icon + gap + bar. */
    static int exxosTileIconGap(int iconSize);
    QFont exxosTileFont() const;
    /** Slightly smaller: the hardware line and the free-space line. */
    QFont exxosTileSmallFont() const;
    /** The Solid UDI of the drive this tile shows, or empty. */
    QString exxosDeviceUdi() const;
    /** A turning ring of dots over the icon while that drive is being read. */
    void exxosDrawBusySpinner(QPainter* painter) const;

protected:
    /**
     * Invalidates the cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item need to gets repainted.
     */
    void invalidateCache();

    /**
     * Invalidates the icon cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item needs to get repainted.
     */
    void invalidateIconCache();

    /**
     * Is called if the cache got invalidated by KStandardItemListWidget::invalidateCache().
     * The default implementation is empty.
     */
    virtual void refreshCache();

    /**
     * @return True if the give role should be right aligned when showing it inside a column.
     *         Per default false is returned.
     */
    virtual bool isRoleRightAligned(const QByteArray& role) const;

    /**
     * @return True if the item should be visually marked as hidden item. Per default
     *         false is returned.
     */
    virtual bool isHidden() const;

    /**
     * @return A font based on baseFont which is customized according to the data shown in the widget.
     */
    virtual QFont customizedFont(const QFont& baseFont) const;

    virtual QPalette::ColorRole normalTextColorRole() const;

    void setTextColor(const QColor& color);
    QColor textColor() const;

    void setOverlay(const QPixmap& overlay);
    QPixmap overlay() const;

    /**
     * @see KStandardItemListWidgetInformant::roleText().
     */
    QString roleText(const QByteArray& role, const QHash<QByteArray, QVariant>& values) const;

    /**
     * Fixes:
     * Select the text without MIME-type extension
     * This is file-item-specific and should be moved
     * into KFileItemListWidget.
     *
     * Inherited classes can define, if the MIME-type extension
     * should be selected or not.
     *
     * @return Selection length (with or without MIME-type extension)
     */
    virtual int selectionLength(const QString& text) const;

    void dataChanged(const QHash<QByteArray, QVariant>& current, const QSet<QByteArray>& roles = QSet<QByteArray>()) override;
    void visibleRolesChanged(const QList<QByteArray>& current, const QList<QByteArray>& previous) override;
    void columnWidthChanged(const QByteArray& role, qreal current, qreal previous) override;
    void sidePaddingChanged(qreal width) override;
    void styleOptionChanged(const KItemListStyleOption& current, const KItemListStyleOption& previous) override;
    void hoveredChanged(bool hovered) override;
    void selectedChanged(bool selected) override;
    void siblingsInformationChanged(const QBitArray& current, const QBitArray& previous) override;
    void editedRoleChanged(const QByteArray& current, const QByteArray& previous) override;
    void iconSizeChanged(int current, int previous) override;
    void resizeEvent(QGraphicsSceneResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent *event) override;

    struct TextInfo
    {
        QPointF pos;
        QStaticText staticText;
    };

public Q_SLOTS:
    void finishRoleEditing();

private Q_SLOTS:
    void slotCutItemsChanged();
    void slotRoleEditingCanceled(const QByteArray& role, const QVariant& value);
    void slotRoleEditingFinished(const QByteArray& role, const QVariant& value);

private:
    void triggerCacheRefreshing();
    void updateExpansionArea();
    void updatePixmapCache();

    void updateTextsCache();
    void updateIconsLayoutTextCache();
    void updateCompactLayoutTextCache();
    void updateDetailsLayoutTextCache();

    /* Exxos/Win7 tile view.
       Lays a row out as  [icon] | bold name / capacity bar / free-space text,
       matching Windows Explorer's tile view.  Used INSTEAD of the normal
       details layout for rows that carry the "freeSpace" and "totalSpace"
       roles; every other row is unaffected. */
    void updateTilesLayoutTextCache();

    /* Exxos: last step of building the icon pixmap, so a subclass can adjust it.
       Default does nothing, so every view is unchanged; PlacesItemListWidget
       overrides it to darken device icons that would otherwise be invisible
       against the panel. Called only when the pixmap is rebuilt, not per paint. */
protected:
    virtual void applyIconEffect(QPixmap& pixmap) const;
private:
    bool hasCapacityInfo() const;
    void drawCapacityBar(QPainter* painter) const;

    void updateAdditionalInfoTextColor();

    void drawPixmap(QPainter* painter, const QPixmap& pixmap);
    void drawSiblingsInformation(QPainter* painter);

    QRectF roleEditingRect(const QByteArray &role) const;

    QString elideRightKeepExtension(const QString &text, int elidingWidth) const;

    /**
     * Closes the role editor and returns the focus back
     * to the KItemListContainer.
     */
    void closeRoleEditor();

    static QPixmap pixmapForIcon(const QString& name, const QStringList& overlays, int size, QIcon::Mode mode);

    /**
     * @return Preferred size of the rating-image based on the given
     *         style-option. The height of the font is taken as
     *         reference.
     */
    static QSizeF preferredRatingSize(const KItemListStyleOption& option);

    /**
     * @return Horizontal padding in pixels that is added to the required width of
     *         a column to display the content.
     */
    static qreal columnPadding(const KItemListStyleOption& option);

protected:
    QHash<QByteArray, TextInfo*> m_textInfo; // PlacesItemListWidget needs to access this

private:
    bool m_isCut;
    bool m_isHidden;
    QFont m_customizedFont;
    QFontMetrics m_customizedFontMetrics;
    bool m_isExpandable;
    bool m_highlightEntireRow;
    bool m_supportsItemExpanding;

    bool m_dirtyLayout;
    bool m_dirtyContent;
    QSet<QByteArray> m_dirtyContentRoles;

    Layout m_layout;

    /* Exxos/Win7 tile view state, valid only while m_isTile is true. */
    bool m_isTile;
    qulonglong m_freeSpace;
    qulonglong m_totalSpace;
    QRectF m_capacityBarRect;
    QPointF m_capacityFreeTextPos;
    QStaticText m_capacityFreeText;
    /* Exxos/Win7: the media's own name, on its own line under what the
       hardware is. Sharing one line clipped whichever came second. */
    QPointF m_exxosLabelTextPos;
    QStaticText m_exxosLabelText;
    QPointF m_pixmapPos;
    QPixmap m_pixmap;
    QSize m_scaledPixmapSize; //Size of the pixmap in device independent pixels

    qreal m_columnWidthSum;
    QRectF m_iconRect;          // Cache for KItemListWidget::iconRect()
    QPixmap m_hoverPixmap;      // Cache for modified m_pixmap when hovering the item

    QRectF m_textRect;

    QList<QByteArray> m_sortedVisibleRoles;

    QRectF m_expansionArea;

    QColor m_customTextColor;
    QColor m_additionalInfoTextColor;

    QPixmap m_overlay;
    QPixmap m_rating;

    KItemListRoleEditor* m_roleEditor;
    KItemListRoleEditor* m_oldRoleEditor;

    friend class KStandardItemListWidgetInformant; // Accesses private static methods to be able to
                                                   // share a common layout calculation
};

#endif
