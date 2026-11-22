/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "ClipboardHistoryItem.h"

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QList>

class QTimer;

namespace deskflow::gui::clipboard {

/**
 * @brief Stores the clipboard history, newest first, and persists it to disk.
 *
 * Copying something that is already in the history moves it back to the top
 * and bumps its copy count instead of adding a duplicate. Pinned items are
 * never trimmed by the size limit or by "clear history".
 */
class ClipboardHistoryModel : public QAbstractListModel
{
  Q_OBJECT
public:
  enum Roles
  {
    IdRole = Qt::UserRole + 1,
    KindRole,
    TitleRole,
    TextRole,
    SearchTextRole,
    PinnedRole,
    FirstCopiedRole,
    LastCopiedRole,
    CopyCountRole,
    SourceRole,
    ThumbnailRole,
    ColorRole
  };

  /**
   * @param storageDir directory for the history database and image files.
   *        An empty string keeps the history in memory only.
   */
  explicit ClipboardHistoryModel(const QString &storageDir, QObject *parent = nullptr);
  ~ClipboardHistoryModel() override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  /**
   * @brief Add an item or, if one with the same hash exists, move it to the top.
   * @param image full size image for image items, written to disk
   * @return the id of the item that is now at the top
   */
  QString add(HistoryItem item, const QImage &image = QImage());

  /// Mark an item as copied again: move it to the top and bump its count.
  void touch(const QString &id);
  void setPinned(const QString &id, bool pinned);
  void setSource(const QString &id, const QString &source);
  void remove(const QString &id);
  /// Remove everything, or everything except pinned items.
  void clear(bool keepPinned = true);

  void setMaxItems(int maxItems);
  int maxItems() const
  {
    return m_maxItems;
  }

  int rowOf(const QString &id) const;
  const HistoryItem *item(const QString &id) const;
  const HistoryItem *itemAt(int row) const;
  QImage image(const QString &id) const;

  QString storageDir() const
  {
    return m_storageDir;
  }

  /// Write pending changes to disk now rather than after the debounce.
  void saveNow();

  /// Item from the model's in memory list, suitable for tests.
  const QList<HistoryItem> &items() const
  {
    return m_items;
  }

  static constexpr int kThumbnailSize = 72;

Q_SIGNALS:
  void itemAdded(const QString &id);

private:
  void load();
  void scheduleSave();
  void trim();
  void removeRow(int row);
  void moveToTop(int row);
  QString imagePath(const QString &fileName) const;
  QString databasePath() const;

  QList<HistoryItem> m_items;
  QString m_storageDir;
  int m_maxItems = 200;
  QTimer *m_saveTimer = nullptr;
  mutable QHash<QString, QImage> m_thumbnails;
};

} // namespace deskflow::gui::clipboard
