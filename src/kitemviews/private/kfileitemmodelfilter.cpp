/*
 * SPDX-FileCopyrightText: 2011 Janardhan Reddy <annapareddyjanardhanreddy@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kfileitemmodelfilter.h"

#include <QRegularExpression>

#include <KFileItem>

KFileItemModelFilter::KFileItemModelFilter() :
    m_useRegExp(false),
    m_regExp(nullptr),
    m_lowerCasePattern(),
    m_pattern(),
    m_searchRegExp(nullptr),
    m_searchPattern()
{
}

KFileItemModelFilter::~KFileItemModelFilter()
{
    delete m_regExp;
    m_regExp = nullptr;
    delete m_searchRegExp;
    m_searchRegExp = nullptr;
}

void KFileItemModelFilter::setPattern(const QString& filter)
{
    m_pattern = filter;
    m_lowerCasePattern = filter.toLower();

    if (filter.contains('*') || filter.contains('?') || filter.contains('[')) {
        if (!m_regExp) {
            m_regExp = new QRegularExpression();
            m_regExp->setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        m_regExp->setPattern(QRegularExpression::wildcardToRegularExpression(filter));
        m_useRegExp = m_regExp->isValid();
    } else {
        m_useRegExp = false;
    }
}

QString KFileItemModelFilter::pattern() const
{
    return m_pattern;
}

void KFileItemModelFilter::setMimeTypes(const QStringList& types)
{
    m_mimeTypes = types;
}

QStringList KFileItemModelFilter::mimeTypes() const
{
    return m_mimeTypes;
}

bool KFileItemModelFilter::hasSetFilters() const
{
    return !m_searchPattern.isEmpty() || (!m_pattern.isEmpty() || !m_mimeTypes.isEmpty());
}


bool KFileItemModelFilter::matches(const KFileItem& item) const
{
    // The search pattern is not one of the user's filters - it is what was
    // typed in the search box - so it applies on top of whatever else is set.
    if (!m_searchPattern.isEmpty() && !matchesSearchPattern(item)) {
        return false;
    }

    const bool hasPatternFilter = !m_pattern.isEmpty();
    const bool hasMimeTypesFilter = !m_mimeTypes.isEmpty();

    // If no filter is set, return true.
    if (!hasPatternFilter && !hasMimeTypesFilter) {
        return true;
    }

    // If both filters are set, return true when both filters are matched
    if (hasPatternFilter && hasMimeTypesFilter) {
        return (matchesPattern(item) && matchesType(item));
    }

    // If only one filter is set, return true when that filter is matched
    if (hasPatternFilter) {
        return matchesPattern(item);
    }

    return matchesType(item);
}

bool KFileItemModelFilter::matchesPattern(const KFileItem& item) const
{
    if (m_useRegExp) {
        return m_regExp->match(item.text()).hasMatch();
    } else {
        return item.text().toLower().contains(m_lowerCasePattern);
    }
}

void KFileItemModelFilter::setSearchPattern(const QString& pattern)
{
    m_searchPattern = pattern;

    if (pattern.isEmpty()) {
        return;
    }

    if (!m_searchRegExp) {
        m_searchRegExp = new QRegularExpression();
        m_searchRegExp->setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    // A search without any wildcard in it keeps the substring behaviour people
    // expect from a search box: typing "mid" finds anything with mid in it.
    // Once a wildcard appears, the whole name has to match, so *.mid means
    // what it means everywhere else.
    if (pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'))
        || pattern.contains(QLatin1Char('['))) {
        m_searchRegExp->setPattern(QRegularExpression::wildcardToRegularExpression(pattern));
    } else {
        m_searchRegExp->setPattern(QRegularExpression::escape(pattern));
    }

    if (!m_searchRegExp->isValid()) {
        m_searchPattern.clear();
    }
}

QString KFileItemModelFilter::searchPattern() const
{
    return m_searchPattern;
}

bool KFileItemModelFilter::matchesSearchPattern(const KFileItem& item) const
{
    if (!m_searchRegExp || m_searchPattern.isEmpty()) {
        return true;
    }
    return m_searchRegExp->match(item.text()).hasMatch();
}

bool KFileItemModelFilter::matchesType(const KFileItem& item) const
{
    for (const QString& mimeType : qAsConst(m_mimeTypes)) {
        if (item.mimetype() == mimeType) {
            return true;
        }
    }

    return m_mimeTypes.isEmpty();
}
