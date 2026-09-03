/*
 * SPDX-FileCopyrightText: 2011 Janardhan Reddy <annapareddyjanardhanreddy@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KFILEITEMMODELFILTER_H
#define KFILEITEMMODELFILTER_H

#include "dolphin_export.h"

#include <QStringList>

class KFileItem;
class QRegularExpression;

/**
 * @brief Allows to check whether an item of the KFileItemModel
 *        matches with a set filter-string.
 *
 * Currently the filter is only checked for the KFileItem::text()
 * property of the KFileItem, but this might get extended in
 * future.
 */
class DOLPHIN_EXPORT KFileItemModelFilter
{

public:
    KFileItemModelFilter();
    virtual ~KFileItemModelFilter();

    /**
     * Sets the pattern that is used for a comparison with the item
     * in KFileItemModelFilter::matches(). Per default the pattern
     * defines a sub-string. As soon as the pattern contains at least
     * a '*', '?' or '[' the pattern represents a regular expression.
     */
    void setPattern(const QString& pattern);
    QString pattern() const;

    /**
     * Set the list of mimetypes that are used for comparison with the
     * item in KFileItemModelFilter::matchesMimeType.
     */
    /**
     * Sets the wildcard pattern that came from the search box, kept separate
     * from setPattern() so that searching does not disturb the filter bar and
     * vice versa. Matched with wildcard semantics: *.mid means files whose
     * name ends in .mid, not files with "mid" somewhere in them.
     */
    void setSearchPattern(const QString& pattern);
    QString searchPattern() const;

    void setMimeTypes(const QStringList& types);
    QStringList mimeTypes() const;

    /**
     * @return True if either the pattern or mimetype filters has been set.
     */
    bool hasSetFilters() const;

    /**
     * @return True if the item matches with the pattern defined by
     *         @ref setPattern() or @ref setMimeTypes
     */
    bool matches(const KFileItem& item) const;

private:
    /**
     * @return True if item matches pattern set by @ref setPattern.
     */
    bool matchesPattern(const KFileItem& item) const;

    /**
     * @return True if item matches mimetypes set by @ref setMimeTypes.
     */
    bool matchesType(const KFileItem& item) const;

    /**
     * @return True if item matches the wildcard set by @ref setSearchPattern.
     */
    bool matchesSearchPattern(const KFileItem& item) const;

    bool m_useRegExp;           // If true, m_regExp is used for filtering,
                                // otherwise m_lowerCaseFilter is used.
    QRegularExpression *m_regExp;
    QString m_lowerCasePattern; // Lowercase version of m_filter for
                                // faster comparison in matches().
    QString m_pattern;          // Property set by setPattern().
    QStringList m_mimeTypes;    // Property set by setMimeTypes()
    QRegularExpression *m_searchRegExp; // Wildcard from the search box
    QString m_searchPattern;    // Property set by setSearchPattern()
};
#endif


