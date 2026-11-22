/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryItem.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>

namespace deskflow::gui::clipboard {

namespace {
QString fileNameFromUrl(const QString &url)
{
  const QUrl u(url);
  const auto name = u.isLocalFile() ? QFileInfo(u.toLocalFile()).fileName() : u.fileName();
  return name.isEmpty() ? url : name;
}
} // namespace

QString HistoryItem::title() const
{
  switch (kind) {
  case ItemKind::Image:
    if (imageSize.isValid())
      return QCoreApplication::translate("ClipboardHistory", "Image %1 × %2")
          .arg(imageSize.width())
          .arg(imageSize.height());
    return QCoreApplication::translate("ClipboardHistory", "Image");

  case ItemKind::Files: {
    if (urls.isEmpty())
      return QCoreApplication::translate("ClipboardHistory", "Files");
    auto first = fileNameFromUrl(urls.first());
    if (urls.size() == 1)
      return first;
    return QCoreApplication::translate("ClipboardHistory", "%1 and %2 more").arg(first).arg(urls.size() - 1);
  }

  default:
    break;
  }

  // First non-blank line, with runs of whitespace collapsed.
  const auto lines = QStringView(text).split(QLatin1Char('\n'));
  for (const auto &line : lines) {
    const auto simplified = line.toString().simplified();
    if (!simplified.isEmpty())
      return simplified.left(500);
  }
  return text.simplified().left(500);
}

QString HistoryItem::searchText() const
{
  if (kind == ItemKind::Files)
    return urls.join(QLatin1Char('\n'));
  if (kind == ItemKind::Image)
    return title();
  return text;
}

QJsonObject HistoryItem::toJson() const
{
  QJsonObject o;
  o[QStringLiteral("id")] = id;
  o[QStringLiteral("kind")] = kindToString(kind);
  if (!text.isEmpty())
    o[QStringLiteral("text")] = text;
  if (!html.isEmpty())
    o[QStringLiteral("html")] = html;
  if (!urls.isEmpty())
    o[QStringLiteral("urls")] = QJsonArray::fromStringList(urls);
  if (!imageFile.isEmpty()) {
    o[QStringLiteral("image")] = imageFile;
    o[QStringLiteral("imageWidth")] = imageSize.width();
    o[QStringLiteral("imageHeight")] = imageSize.height();
  }
  o[QStringLiteral("hash")] = QString::fromLatin1(hash.toHex());
  o[QStringLiteral("firstCopied")] = firstCopied.toString(Qt::ISODateWithMs);
  o[QStringLiteral("lastCopied")] = lastCopied.toString(Qt::ISODateWithMs);
  o[QStringLiteral("copyCount")] = copyCount;
  if (pinned)
    o[QStringLiteral("pinned")] = true;
  if (!source.isEmpty())
    o[QStringLiteral("source")] = source;
  return o;
}

HistoryItem HistoryItem::fromJson(const QJsonObject &o)
{
  HistoryItem item;
  item.id = o.value(QStringLiteral("id")).toString();
  item.kind = kindFromString(o.value(QStringLiteral("kind")).toString());
  item.text = o.value(QStringLiteral("text")).toString();
  item.html = o.value(QStringLiteral("html")).toString();
  for (const auto &url : o.value(QStringLiteral("urls")).toArray())
    item.urls.append(url.toString());
  item.imageFile = o.value(QStringLiteral("image")).toString();
  if (!item.imageFile.isEmpty())
    item.imageSize =
        QSize(o.value(QStringLiteral("imageWidth")).toInt(), o.value(QStringLiteral("imageHeight")).toInt());
  item.hash = QByteArray::fromHex(o.value(QStringLiteral("hash")).toString().toLatin1());
  item.firstCopied = QDateTime::fromString(o.value(QStringLiteral("firstCopied")).toString(), Qt::ISODateWithMs);
  item.lastCopied = QDateTime::fromString(o.value(QStringLiteral("lastCopied")).toString(), Qt::ISODateWithMs);
  item.copyCount = qMax(1, o.value(QStringLiteral("copyCount")).toInt(1));
  item.pinned = o.value(QStringLiteral("pinned")).toBool(false);
  item.source = o.value(QStringLiteral("source")).toString();
  if (!item.lastCopied.isValid())
    item.lastCopied = QDateTime::currentDateTime();
  if (!item.firstCopied.isValid())
    item.firstCopied = item.lastCopied;
  return item;
}

QString HistoryItem::kindToString(ItemKind kind)
{
  switch (kind) {
  case ItemKind::Link:
    return QStringLiteral("link");
  case ItemKind::Color:
    return QStringLiteral("color");
  case ItemKind::Image:
    return QStringLiteral("image");
  case ItemKind::Files:
    return QStringLiteral("files");
  case ItemKind::Text:
    break;
  }
  return QStringLiteral("text");
}

ItemKind HistoryItem::kindFromString(const QString &kind)
{
  if (kind == QLatin1String("link"))
    return ItemKind::Link;
  if (kind == QLatin1String("color"))
    return ItemKind::Color;
  if (kind == QLatin1String("image"))
    return ItemKind::Image;
  if (kind == QLatin1String("files"))
    return ItemKind::Files;
  return ItemKind::Text;
}

} // namespace deskflow::gui::clipboard
