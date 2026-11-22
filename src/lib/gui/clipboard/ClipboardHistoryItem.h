/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

namespace deskflow::gui::clipboard {

enum class ItemKind
{
  Text,
  Link,
  Color,
  Image,
  Files
};

/**
 * @brief One entry in the clipboard history.
 *
 * Text, rich text and file lists are kept inline; images are stored as PNG
 * files next to the history database and referenced by name.
 */
struct HistoryItem
{
  QString id;
  ItemKind kind = ItemKind::Text;
  QString text;
  QString html;
  QStringList urls;
  QString imageFile;
  QSize imageSize;
  QByteArray hash;
  QDateTime firstCopied;
  QDateTime lastCopied;
  int copyCount = 1;
  bool pinned = false;
  /// Name of the computer the content came from, empty when it was copied on this computer.
  QString source;

  /// A single line suitable for list rows.
  QString title() const;
  /// Everything that search should look at.
  QString searchText() const;

  QJsonObject toJson() const;
  static HistoryItem fromJson(const QJsonObject &object);

  static QString kindToString(ItemKind kind);
  static ItemKind kindFromString(const QString &kind);
};

} // namespace deskflow::gui::clipboard
