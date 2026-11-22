/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryDelegate.h"

#include "ClipboardHistoryItem.h"
#include "ClipboardHistoryModel.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace deskflow::gui::clipboard {

namespace {

QColor withAlpha(QColor color, qreal alpha)
{
  color.setAlphaF(float(alpha));
  return color;
}

QFont scaledFont(QFont font, qreal factor)
{
  if (font.pointSizeF() > 0)
    font.setPointSizeF(font.pointSizeF() * factor);
  else if (font.pixelSize() > 0)
    font.setPixelSize(qRound(font.pixelSize() * factor));
  return font;
}

bool isDark(const QPalette &palette)
{
  return palette.color(QPalette::Window).lightness() < 128;
}

QColor kindTint(ItemKind kind)
{
  switch (kind) {
  case ItemKind::Link:
    return QColor(0x3b, 0x82, 0xf6); // blue
  case ItemKind::Files:
    return QColor(0xf5, 0x9e, 0x0b); // amber
  case ItemKind::Image:
    return QColor(0x10, 0xb9, 0x81); // green
  case ItemKind::Color:
  case ItemKind::Text:
    break;
  }
  return QColor(0x8b, 0x5c, 0xf6); // violet
}

void drawBadge(
    QPainter *p, const QRectF &rect, ItemKind kind, const QModelIndex &index, bool selected, const QPalette &palette
)
{
  QPainterPath clip;
  clip.addRoundedRect(rect, 9, 9);

  if (kind == ItemKind::Image) {
    const auto thumb = index.data(ClipboardHistoryModel::ThumbnailRole).value<QImage>();
    if (!thumb.isNull()) {
      p->save();
      p->setClipPath(clip);
      const auto scaled = thumb.scaled(
          rect.size().toSize() * p->device()->devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
          Qt::SmoothTransformation
      );
      QRectF source(QPointF(0, 0), rect.size() * p->device()->devicePixelRatioF());
      source.moveCenter(QPointF(scaled.width() / 2.0, scaled.height() / 2.0));
      p->drawImage(rect, scaled, source);
      p->restore();
      p->setPen(QPen(withAlpha(palette.color(QPalette::Text), 0.12), 1));
      p->setBrush(Qt::NoBrush);
      p->drawPath(clip);
      return;
    }
  }

  if (kind == ItemKind::Color) {
    const auto color = index.data(ClipboardHistoryModel::ColorRole).value<QColor>();
    if (color.isValid()) {
      p->setPen(QPen(withAlpha(palette.color(QPalette::Text), 0.18), 1));
      p->setBrush(color);
      p->drawPath(clip);
      return;
    }
  }

  const auto tint = kindTint(kind);
  const auto fg = selected ? palette.color(QPalette::HighlightedText) : tint;
  p->setPen(Qt::NoPen);
  p->setBrush(
      selected ? withAlpha(palette.color(QPalette::HighlightedText), 0.18)
               : withAlpha(tint, isDark(palette) ? 0.22 : 0.14)
  );
  p->drawPath(clip);

  p->setPen(QPen(fg, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p->setBrush(Qt::NoBrush);
  const auto c = rect.center();

  switch (kind) {
  case ItemKind::Link: {
    // A chain link: two rounded capsules at 45 degrees.
    p->save();
    p->translate(c);
    p->rotate(-45);
    p->drawRoundedRect(QRectF(-9.5, -3.5, 11, 7), 3.5, 3.5);
    p->drawRoundedRect(QRectF(-1.5, -3.5, 11, 7), 3.5, 3.5);
    p->restore();
    break;
  }
  case ItemKind::Files: {
    // A page with a folded corner.
    QPainterPath page;
    const QRectF r(c.x() - 7, c.y() - 9, 14, 18);
    page.moveTo(r.left(), r.top());
    page.lineTo(r.right() - 5, r.top());
    page.lineTo(r.right(), r.top() + 5);
    page.lineTo(r.right(), r.bottom());
    page.lineTo(r.left(), r.bottom());
    page.closeSubpath();
    p->drawPath(page);
    p->drawLine(QPointF(r.right() - 5, r.top()), QPointF(r.right() - 5, r.top() + 5));
    p->drawLine(QPointF(r.right() - 5, r.top() + 5), QPointF(r.right(), r.top() + 5));
    break;
  }
  case ItemKind::Image: {
    // Picture frame with a mountain.
    const QRectF r(c.x() - 9, c.y() - 7, 18, 14);
    p->drawRoundedRect(r, 2.5, 2.5);
    QPainterPath m;
    m.moveTo(r.left() + 2, r.bottom() - 2);
    m.lineTo(r.left() + 7, r.top() + 6);
    m.lineTo(r.left() + 11, r.bottom() - 4);
    m.lineTo(r.left() + 13, r.bottom() - 6);
    m.lineTo(r.right() - 2, r.bottom() - 2);
    p->drawPath(m);
    break;
  }
  default: {
    auto font = QApplication::font();
    font.setPixelSize(14);
    font.setWeight(QFont::DemiBold);
    p->setFont(font);
    p->drawText(rect, Qt::AlignCenter, QStringLiteral("Aa"));
    break;
  }
  }
}

} // namespace

QString relativeTime(const QDateTime &time, const QDateTime &now)
{
  if (!time.isValid())
    return {};
  const auto secs = time.secsTo(now);
  if (secs < 45)
    return QCoreApplication::translate("ClipboardHistory", "just now");
  if (secs < 3600)
    return QCoreApplication::translate("ClipboardHistory", "%1 min ago").arg(qMax<qint64>(1, secs / 60));
  if (secs < 6 * 3600 || time.date() == now.date())
    return QCoreApplication::translate("ClipboardHistory", "%1 h ago").arg(secs / 3600);
  if (time.date() == now.date().addDays(-1))
    return QCoreApplication::translate("ClipboardHistory", "yesterday");
  if (time.date().daysTo(now.date()) < 7)
    return QLocale().toString(time.date(), QStringLiteral("dddd"));
  if (time.date().year() == now.date().year())
    return QLocale().toString(time.date(), QStringLiteral("MMM d"));
  return QLocale().toString(time.date(), QLocale::ShortFormat);
}

QString quickPickHint(int number)
{
#if defined(Q_OS_MACOS)
  return QStringLiteral("⌘%1").arg(number);
#else
  return QStringLiteral("Ctrl+%1").arg(number);
#endif
}

QSize ClipboardHistoryDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
  Q_UNUSED(index)
  return QSize(option.rect.width(), kRowHeight);
}

void ClipboardHistoryDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
  p->save();
  p->setRenderHint(QPainter::Antialiasing);
  p->setRenderHint(QPainter::TextAntialiasing);

  const auto &palette = option.palette;
  const bool selected = option.state & QStyle::State_Selected;
  const bool hovered = option.state & QStyle::State_MouseOver;
  const auto text = palette.color(QPalette::Text);
  const auto fg = selected ? palette.color(QPalette::HighlightedText) : text;
  const auto muted = withAlpha(fg, selected ? 0.78 : 0.55);

  const QRectF row = QRectF(option.rect).adjusted(8, 2, -8, -2);
  if (selected) {
    p->setPen(Qt::NoPen);
    p->setBrush(palette.color(QPalette::Highlight));
    p->drawRoundedRect(row, 10, 10);
  } else if (hovered) {
    p->setPen(Qt::NoPen);
    p->setBrush(withAlpha(text, 0.06));
    p->drawRoundedRect(row, 10, 10);
  }

  const auto kind = ItemKind(index.data(ClipboardHistoryModel::KindRole).toInt());
  const QRectF badge(row.left() + 10, row.center().y() - kBadgeSize / 2.0, kBadgeSize, kBadgeSize);
  drawBadge(p, badge, kind, index, selected, palette);

  // Right side: pin marker and quick pick hint.
  qreal right = row.right() - 12;
  const auto smallFont = scaledFont(option.font, 0.85);

  if (index.row() < 9) {
    const auto hint = quickPickHint(index.row() + 1);
    const QFontMetricsF fm(smallFont);
    const qreal w = fm.horizontalAdvance(hint) + 10;
    const QRectF chip(right - w, row.center().y() - 10, w, 20);
    p->setPen(Qt::NoPen);
    p->setBrush(withAlpha(fg, selected ? 0.18 : 0.07));
    p->drawRoundedRect(chip, 5, 5);
    p->setPen(muted);
    p->setFont(smallFont);
    p->drawText(chip, Qt::AlignCenter, hint);
    right = chip.left() - 8;
  }

  if (index.data(ClipboardHistoryModel::PinnedRole).toBool()) {
    p->setFont(scaledFont(option.font, 1.1));
    p->setPen(selected ? fg : QColor(0xf5, 0xb0, 0x0b));
    const QRectF star(right - 18, row.top(), 18, row.height());
    p->drawText(star, Qt::AlignCenter, QStringLiteral("★"));
    right = star.left() - 6;
  }

  // Title and subtitle.
  const qreal left = badge.right() + 12;
  const qreal width = qMax<qreal>(0, right - left);

  auto titleFont = option.font;
  titleFont.setWeight(QFont::Medium);
  const QFontMetricsF titleMetrics(titleFont);
  const auto title =
      titleMetrics.elidedText(index.data(ClipboardHistoryModel::TitleRole).toString(), Qt::ElideRight, width);

  QStringList meta;
  const auto source = index.data(ClipboardHistoryModel::SourceRole).toString();
  if (!source.isEmpty())
    meta << QCoreApplication::translate("ClipboardHistory", "from %1").arg(source);
  meta << relativeTime(index.data(ClipboardHistoryModel::LastCopiedRole).toDateTime());
  const int count = index.data(ClipboardHistoryModel::CopyCountRole).toInt();
  if (count > 1)
    meta << QCoreApplication::translate("ClipboardHistory", "copied %1×").arg(count);

  const QFontMetricsF smallMetrics(smallFont);
  const auto subtitle = smallMetrics.elidedText(meta.join(QStringLiteral("  ·  ")), Qt::ElideRight, width);

  const qreal blockHeight = titleMetrics.height() + 3 + smallMetrics.height();
  const qreal top = row.center().y() - blockHeight / 2;

  p->setFont(titleFont);
  p->setPen(fg);
  p->drawText(QRectF(left, top, width, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, title);

  p->setFont(smallFont);
  p->setPen(muted);
  p->drawText(
      QRectF(left, top + titleMetrics.height() + 3, width, smallMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
      subtitle
  );

  p->restore();
}

} // namespace deskflow::gui::clipboard
