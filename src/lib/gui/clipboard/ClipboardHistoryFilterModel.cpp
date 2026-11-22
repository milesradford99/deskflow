/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryFilterModel.h"

#include "ClipboardHistoryItem.h"
#include "ClipboardHistoryModel.h"

#include <QDateTime>

namespace deskflow::gui::clipboard {

namespace {

int wordScore(const QString &haystack, QStringView word)
{
  // Substring match: prefer a match at the start of a word and near the start.
  qsizetype from = 0;
  int best = -1;
  while (true) {
    const auto pos = haystack.indexOf(word, from, Qt::CaseInsensitive);
    if (pos < 0)
      break;
    const bool wordStart = pos == 0 || !haystack.at(pos - 1).isLetterOrNumber();
    const int score = 1000 + (wordStart ? 500 : 0) - int(qMin<qsizetype>(pos, 400));
    best = qMax(best, score);
    if (wordStart)
      break;
    from = pos + 1;
  }
  if (best >= 0)
    return best;

  // Fuzzy subsequence match, penalised by how spread out it is.
  qsizetype hay = 0;
  qsizetype first = -1;
  qsizetype last = -1;
  for (const auto ch : word) {
    const auto lower = ch.toLower();
    while (hay < haystack.size() && haystack.at(hay).toLower() != lower)
      ++hay;
    if (hay >= haystack.size())
      return -1;
    if (first < 0)
      first = hay;
    last = hay++;
  }
  const auto spread = last - first + 1 - word.size();
  return qMax<int>(1, 500 - int(qMin<qsizetype>(spread * 10, 490)));
}

} // namespace

ClipboardHistoryFilterModel::ClipboardHistoryFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
  setDynamicSortFilter(true);
  sort(0);
}

void ClipboardHistoryFilterModel::setSearchText(const QString &text)
{
  if (m_searchText == text)
    return;
  m_searchText = text;
  refresh();
}

void ClipboardHistoryFilterModel::setCategory(Category category)
{
  if (m_category == category)
    return;
  m_category = category;
  refresh();
}

void ClipboardHistoryFilterModel::setSortMode(SortMode mode)
{
  if (m_sortMode == mode)
    return;
  m_sortMode = mode;
  invalidate();
}

int ClipboardHistoryFilterModel::matchScore(const QString &haystack, const QString &needle)
{
  const auto words = QStringView(needle).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (words.isEmpty())
    return 0;

  int total = 0;
  for (const auto &word : words) {
    const int score = wordScore(haystack, word);
    if (score < 0)
      return -1;
    total += score;
  }
  return total / int(words.size());
}

bool ClipboardHistoryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
  const auto index = sourceModel()->index(sourceRow, 0, sourceParent);
  const auto kind = ItemKind(index.data(ClipboardHistoryModel::KindRole).toInt());

  switch (m_category) {
  case Category::All:
    break;
  case Category::Pinned:
    if (!index.data(ClipboardHistoryModel::PinnedRole).toBool())
      return false;
    break;
  case Category::Text:
    if (kind != ItemKind::Text && kind != ItemKind::Color)
      return false;
    break;
  case Category::Links:
    if (kind != ItemKind::Link)
      return false;
    break;
  case Category::Images:
    if (kind != ItemKind::Image)
      return false;
    break;
  case Category::Files:
    if (kind != ItemKind::Files)
      return false;
    break;
  }

  return m_searchText.trimmed().isEmpty() || scoreFor(index) >= 0;
}

bool ClipboardHistoryFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
  // "less than" means "shown first" since we sort ascending.
  const bool leftPinned = left.data(ClipboardHistoryModel::PinnedRole).toBool();
  const bool rightPinned = right.data(ClipboardHistoryModel::PinnedRole).toBool();
  if (leftPinned != rightPinned)
    return leftPinned;

  if (!m_searchText.trimmed().isEmpty()) {
    // Bucket scores so that a slightly earlier match does not beat recency.
    const int leftScore = scoreFor(left) / 250;
    const int rightScore = scoreFor(right) / 250;
    if (leftScore != rightScore)
      return leftScore > rightScore;
  }

  const auto leftLast = left.data(ClipboardHistoryModel::LastCopiedRole).toDateTime();
  const auto rightLast = right.data(ClipboardHistoryModel::LastCopiedRole).toDateTime();

  switch (m_sortMode) {
  case SortMode::MostUsed: {
    const int l = left.data(ClipboardHistoryModel::CopyCountRole).toInt();
    const int r = right.data(ClipboardHistoryModel::CopyCountRole).toInt();
    if (l != r)
      return l > r;
    break;
  }
  case SortMode::FirstCopied: {
    const auto l = left.data(ClipboardHistoryModel::FirstCopiedRole).toDateTime();
    const auto r = right.data(ClipboardHistoryModel::FirstCopiedRole).toDateTime();
    if (l != r)
      return l > r;
    break;
  }
  case SortMode::Recent:
    break;
  }

  if (leftLast != rightLast)
    return leftLast > rightLast;
  return left.row() < right.row();
}

int ClipboardHistoryFilterModel::scoreFor(const QModelIndex &sourceIndex) const
{
  const auto id = sourceIndex.data(ClipboardHistoryModel::IdRole).toString();
  auto it = m_scores.constFind(id);
  if (it != m_scores.constEnd())
    return *it;

  const auto haystack = sourceIndex.data(ClipboardHistoryModel::SearchTextRole).toString() + QLatin1Char('\n') +
                        sourceIndex.data(ClipboardHistoryModel::SourceRole).toString();
  const int score = matchScore(haystack, m_searchText.trimmed());
  m_scores.insert(id, score);
  return score;
}

void ClipboardHistoryFilterModel::refresh()
{
  m_scores.clear();
  invalidate();
}

} // namespace deskflow::gui::clipboard
