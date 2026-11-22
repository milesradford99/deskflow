/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QHash>
#include <QSortFilterProxyModel>

namespace deskflow::gui::clipboard {

/**
 * @brief Search, category filter and ordering for the clipboard history.
 *
 * Pinned items always come first. While searching, closer matches rank above
 * looser ones and the chosen sort order breaks ties.
 */
class ClipboardHistoryFilterModel : public QSortFilterProxyModel
{
  Q_OBJECT
public:
  enum class Category
  {
    All,
    Pinned,
    Text,
    Links,
    Images,
    Files
  };
  Q_ENUM(Category)

  enum class SortMode
  {
    Recent,
    MostUsed,
    FirstCopied
  };
  Q_ENUM(SortMode)

  explicit ClipboardHistoryFilterModel(QObject *parent = nullptr);

  void setSearchText(const QString &text);
  QString searchText() const
  {
    return m_searchText;
  }

  void setCategory(Category category);
  Category category() const
  {
    return m_category;
  }

  void setSortMode(SortMode mode);
  SortMode sortMode() const
  {
    return m_sortMode;
  }

  /**
   * @brief Score how well @p needle matches @p haystack, case insensitively.
   *
   * Every whitespace separated word of the needle must match, either as a
   * substring (best when it starts a word) or as an in order subsequence.
   * @return -1 when it does not match, otherwise higher is better
   */
  static int matchScore(const QString &haystack, const QString &needle);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
  int scoreFor(const QModelIndex &sourceIndex) const;
  void refresh();

  QString m_searchText;
  Category m_category = Category::All;
  SortMode m_sortMode = SortMode::Recent;
  mutable QHash<QString, int> m_scores;
};

} // namespace deskflow::gui::clipboard
