/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDateTime>
#include <QStyledItemDelegate>

namespace deskflow::gui::clipboard {

/// "just now", "5 min ago", "yesterday"...
QString relativeTime(const QDateTime &time, const QDateTime &now = QDateTime::currentDateTime());

/// Text for a "Ctrl+1" style hint, using the platform's modifier symbol.
QString quickPickHint(int number);

/**
 * @brief Paints a clipboard history row: a type badge or thumbnail, a one line
 * preview, where and when it was copied, and pin / quick pick markers.
 */
class ClipboardHistoryDelegate : public QStyledItemDelegate
{
  Q_OBJECT
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  static constexpr int kRowHeight = 58;
  static constexpr int kBadgeSize = 38;
};

} // namespace deskflow::gui::clipboard
