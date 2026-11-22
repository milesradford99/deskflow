/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryModel.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace deskflow::gui::clipboard {

namespace {
constexpr int kDatabaseVersion = 1;
const auto kDatabaseName = QStringLiteral("history.json");
const auto kImagesDir = QStringLiteral("images");
constexpr auto kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
} // namespace

ClipboardHistoryModel::ClipboardHistoryModel(const QString &storageDir, QObject *parent)
    : QAbstractListModel(parent),
      m_storageDir(storageDir),
      m_saveTimer(new QTimer(this))
{
  m_saveTimer->setSingleShot(true);
  m_saveTimer->setInterval(750);
  connect(m_saveTimer, &QTimer::timeout, this, &ClipboardHistoryModel::saveNow);
  load();
}

ClipboardHistoryModel::~ClipboardHistoryModel()
{
  if (m_saveTimer->isActive())
    saveNow();
}

int ClipboardHistoryModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : int(m_items.size());
}

QVariant ClipboardHistoryModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
    return {};

  const auto &item = m_items.at(index.row());
  switch (role) {
  case Qt::DisplayRole:
  case TitleRole:
    return item.title();
  case Qt::ToolTipRole:
    return item.kind == ItemKind::Image ? item.title() : item.searchText().left(2000);
  case IdRole:
    return item.id;
  case KindRole:
    return int(item.kind);
  case TextRole:
    return item.text;
  case SearchTextRole:
    return item.searchText();
  case PinnedRole:
    return item.pinned;
  case FirstCopiedRole:
    return item.firstCopied;
  case LastCopiedRole:
    return item.lastCopied;
  case CopyCountRole:
    return item.copyCount;
  case SourceRole:
    return item.source;
  case ColorRole:
    if (item.kind == ItemKind::Color)
      return QColor::fromString(item.text.trimmed());
    return {};
  case ThumbnailRole: {
    if (item.kind != ItemKind::Image)
      return {};
    auto it = m_thumbnails.constFind(item.id);
    if (it != m_thumbnails.constEnd())
      return *it;
    if (item.imageFile.isEmpty())
      return {};
    QImage thumb(imagePath(item.imageFile));
    if (!thumb.isNull())
      thumb = thumb.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_thumbnails.insert(item.id, thumb);
    return thumb;
  }
  default:
    return {};
  }
}

QHash<int, QByteArray> ClipboardHistoryModel::roleNames() const
{
  auto roles = QAbstractListModel::roleNames();
  roles.insert(IdRole, "id");
  roles.insert(KindRole, "kind");
  roles.insert(TitleRole, "title");
  roles.insert(TextRole, "text");
  roles.insert(PinnedRole, "pinned");
  roles.insert(FirstCopiedRole, "firstCopied");
  roles.insert(LastCopiedRole, "lastCopied");
  roles.insert(CopyCountRole, "copyCount");
  roles.insert(SourceRole, "source");
  return roles;
}

QString ClipboardHistoryModel::add(HistoryItem item, const QImage &image)
{
  const auto now = QDateTime::currentDateTime();

  for (int row = 0; row < m_items.size(); ++row) {
    auto &existing = m_items[row];
    if (existing.hash != item.hash)
      continue;
    existing.lastCopied = now;
    existing.copyCount++;
    existing.source = item.source;
    // Keep the richest representation we have seen.
    if (existing.html.isEmpty() && !item.html.isEmpty())
      existing.html = item.html;
    const auto id = existing.id;
    moveToTop(row);
    Q_EMIT dataChanged(index(0), index(0));
    scheduleSave();
    Q_EMIT itemAdded(id);
    return id;
  }

  if (item.id.isEmpty())
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!item.firstCopied.isValid())
    item.firstCopied = now;
  if (!item.lastCopied.isValid())
    item.lastCopied = now;

  if (item.kind == ItemKind::Image && !image.isNull()) {
    item.imageSize = image.size();
    if (!m_storageDir.isEmpty()) {
      item.imageFile = QStringLiteral("%1.png").arg(QString::fromLatin1(item.hash.toHex()));
      QDir().mkpath(imagePath({}));
      const auto path = imagePath(item.imageFile);
      if (image.save(path, "PNG"))
        QFile::setPermissions(path, kOwnerOnly);
      else
        item.imageFile.clear();
    }
    if (item.imageFile.isEmpty()) {
      // No storage, keep a thumbnail so the row still renders.
      m_thumbnails.insert(
          item.id,
          image.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
      );
    }
  }

  beginInsertRows({}, 0, 0);
  m_items.prepend(item);
  endInsertRows();

  trim();
  scheduleSave();
  Q_EMIT itemAdded(item.id);
  return item.id;
}

void ClipboardHistoryModel::touch(const QString &id)
{
  const int row = rowOf(id);
  if (row < 0)
    return;
  m_items[row].lastCopied = QDateTime::currentDateTime();
  m_items[row].copyCount++;
  moveToTop(row);
  Q_EMIT dataChanged(index(0), index(0));
  scheduleSave();
}

void ClipboardHistoryModel::setPinned(const QString &id, bool pinned)
{
  const int row = rowOf(id);
  if (row < 0 || m_items[row].pinned == pinned)
    return;
  m_items[row].pinned = pinned;
  Q_EMIT dataChanged(index(row), index(row), {PinnedRole});
  if (!pinned)
    trim();
  scheduleSave();
}

void ClipboardHistoryModel::setSource(const QString &id, const QString &source)
{
  const int row = rowOf(id);
  if (row < 0 || m_items[row].source == source)
    return;
  m_items[row].source = source;
  Q_EMIT dataChanged(index(row), index(row), {SourceRole});
  scheduleSave();
}

void ClipboardHistoryModel::remove(const QString &id)
{
  const int row = rowOf(id);
  if (row < 0)
    return;
  removeRow(row);
  scheduleSave();
}

void ClipboardHistoryModel::clear(bool keepPinned)
{
  for (int row = int(m_items.size()) - 1; row >= 0; --row) {
    if (keepPinned && m_items.at(row).pinned)
      continue;
    removeRow(row);
  }
  scheduleSave();
}

void ClipboardHistoryModel::setMaxItems(int maxItems)
{
  m_maxItems = qMax(1, maxItems);
  trim();
  scheduleSave();
}

int ClipboardHistoryModel::rowOf(const QString &id) const
{
  for (int row = 0; row < m_items.size(); ++row) {
    if (m_items.at(row).id == id)
      return row;
  }
  return -1;
}

const HistoryItem *ClipboardHistoryModel::item(const QString &id) const
{
  return itemAt(rowOf(id));
}

const HistoryItem *ClipboardHistoryModel::itemAt(int row) const
{
  if (row < 0 || row >= m_items.size())
    return nullptr;
  return &m_items.at(row);
}

QImage ClipboardHistoryModel::image(const QString &id) const
{
  const auto *i = item(id);
  if (!i || i->kind != ItemKind::Image)
    return {};
  if (i->imageFile.isEmpty())
    return m_thumbnails.value(id);
  return QImage(imagePath(i->imageFile));
}

void ClipboardHistoryModel::saveNow()
{
  m_saveTimer->stop();
  if (m_storageDir.isEmpty())
    return;

  if (!QDir().mkpath(m_storageDir)) {
    qWarning() << "clipboard history: unable to create" << m_storageDir;
    return;
  }

  QJsonArray array;
  for (const auto &item : std::as_const(m_items))
    array.append(item.toJson());

  QJsonObject root;
  root[QStringLiteral("version")] = kDatabaseVersion;
  root[QStringLiteral("items")] = array;

  QSaveFile file(databasePath());
  if (!file.open(QIODevice::WriteOnly)) {
    qWarning() << "clipboard history: unable to write" << file.fileName();
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  if (file.commit())
    QFile::setPermissions(databasePath(), kOwnerOnly);
}

void ClipboardHistoryModel::load()
{
  if (m_storageDir.isEmpty())
    return;

  QFile file(databasePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly))
    return;

  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "clipboard history: ignoring unreadable database" << error.errorString();
    return;
  }

  QList<HistoryItem> loaded;
  for (const auto &value : doc.object().value(QStringLiteral("items")).toArray()) {
    auto item = HistoryItem::fromJson(value.toObject());
    if (item.id.isEmpty() || item.hash.isEmpty())
      continue;
    if (item.kind == ItemKind::Image && (item.imageFile.isEmpty() || !QFile::exists(imagePath(item.imageFile))))
      continue;
    loaded.append(item);
  }

  std::stable_sort(loaded.begin(), loaded.end(), [](const HistoryItem &a, const HistoryItem &b) {
    return a.lastCopied > b.lastCopied;
  });

  beginResetModel();
  m_items = loaded;
  endResetModel();
}

void ClipboardHistoryModel::scheduleSave()
{
  if (!m_storageDir.isEmpty())
    m_saveTimer->start();
}

void ClipboardHistoryModel::trim()
{
  int unpinned = 0;
  for (const auto &item : std::as_const(m_items)) {
    if (!item.pinned)
      unpinned++;
  }
  // m_items is newest first, so drop from the end.
  for (int row = int(m_items.size()) - 1; row >= 0 && unpinned > m_maxItems; --row) {
    if (m_items.at(row).pinned)
      continue;
    removeRow(row);
    unpinned--;
  }
}

void ClipboardHistoryModel::removeRow(int row)
{
  const auto item = m_items.at(row);
  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();
  m_thumbnails.remove(item.id);
  if (!item.imageFile.isEmpty())
    QFile::remove(imagePath(item.imageFile));
}

void ClipboardHistoryModel::moveToTop(int row)
{
  if (row <= 0)
    return;
  beginMoveRows({}, row, row, {}, 0);
  m_items.move(row, 0);
  endMoveRows();
}

QString ClipboardHistoryModel::imagePath(const QString &fileName) const
{
  const auto dir = QStringLiteral("%1/%2").arg(m_storageDir, kImagesDir);
  return fileName.isEmpty() ? dir : QStringLiteral("%1/%2").arg(dir, fileName);
}

QString ClipboardHistoryModel::databasePath() const
{
  return QStringLiteral("%1/%2").arg(m_storageDir, kDatabaseName);
}

} // namespace deskflow::gui::clipboard
