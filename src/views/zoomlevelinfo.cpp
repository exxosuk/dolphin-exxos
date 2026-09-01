/*
 * SPDX-FileCopyrightText: 2008 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "zoomlevelinfo.h"

#include <KIconLoader>

#include <QSize>
#include <QtGlobal>

int ZoomLevelInfo::minimumLevel()
{
    /* Exxos/Win7: floor the zoom slider at 32px.
       Levels 0 and 1 are 16px and 22px. At those sizes the tile views cannot
       lay out at all -- they hold a 48px floor of their own -- and even in an
       ordinary folder the icons are too small to identify, so the two lowest
       stops were doing nothing except giving the slider dead travel.
       iconSizeForZoomLevel() and zoomLevelForIconSize() below are clamped to
       match, so a config that already holds 16 or 22 is read back as 32
       instead of landing below the slider's own range. */
    return 2;   // KIconLoader::SizeMedium, 32px
}

int ZoomLevelInfo::maximumLevel()
{
    return 16;
}

int ZoomLevelInfo::iconSizeForZoomLevel(int level)
{
    int size = KIconLoader::SizeMedium;
    switch (qMax(level, minimumLevel())) {
    case 2:  size = KIconLoader::SizeMedium; break;
    case 3:  size = KIconLoader::SizeLarge; break;
    case 4:  size = KIconLoader::SizeHuge; break;
    default: size = KIconLoader::SizeHuge + ((level - 4) << 4);
    }
    return size;
}

int ZoomLevelInfo::zoomLevelForIconSize(const QSize& size)
{
    int level = minimumLevel();
    switch (size.height()) {
    case KIconLoader::SizeSmall:       // fall through: both are below the floor
    case KIconLoader::SizeSmallMedium:
    case KIconLoader::SizeMedium:      level = 2; break;
    case KIconLoader::SizeLarge:       level = 3; break;
    case KIconLoader::SizeHuge:        level = 4; break;
    default: level = 4 + ((size.height() - KIconLoader::SizeHuge) >> 4);
    }
    return qMax(level, minimumLevel());
}
