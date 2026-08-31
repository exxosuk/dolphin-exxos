/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KITEMLISTSTYLEOPTION_H
#define KITEMLISTSTYLEOPTION_H

#include "dolphin_export.h"

#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <QRect>

class DOLPHIN_EXPORT KItemListStyleOption
{
public:
    KItemListStyleOption();
    virtual ~KItemListStyleOption();

    QRect rect;
    QFont font;
    QFontMetrics fontMetrics;
    QPalette palette;
    int padding;
    int horizontalMargin;
    int verticalMargin;
    int iconSize;
    bool extendedSelectionRegion;
    int maxTextLines;
    int maxTextWidth;

    /* Exxos/Win7: the view is laying items out as Explorer-style tiles
       (icon left, text stacked right). Set per-location by
       DolphinItemListView::updateGridSize(). Items WITHOUT capacity data still
       use the tile alignment so they line up with the drives -- they just have
       no bar. */
    bool tileLayout;

    bool operator==(const KItemListStyleOption& other) const;
    bool operator!=(const KItemListStyleOption& other) const;
};
#endif


