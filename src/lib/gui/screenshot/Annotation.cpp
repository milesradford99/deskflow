/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Annotation.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>

namespace deskflow::gui::screenshot {

namespace {

QFont textFont(qreal pixelSize)
{
  QFont font;
  font.setPixelSize(qMax(1, qRound(pixelSize)));
  font.setWeight(QFont::DemiBold);
  return font;
}

qreal arrowHeadLength(qreal size)
{
  return qMax(size * 4.2, 12.0);
}

//! The outline of a stroked shape, before the pen is applied.
QPainterPath strokeShape(const Annotation &a)
{
  QPainterPath path;
  switch (a.tool) {
  case Tool::Rectangle:
    path.addRect(a.rect());
    break;
  case Tool::Ellipse:
    path.addEllipse(a.rect());
    break;
  case Tool::Line:
  case Tool::Arrow:
    path.moveTo(a.start);
    path.lineTo(a.end);
    break;
  case Tool::Pen:
    if (!a.points.isEmpty()) {
      path.moveTo(a.points.first());
      // Smooth the hand drawn line through the midpoints of its samples.
      for (qsizetype i = 1; i + 1 < a.points.size(); ++i)
        path.quadTo(a.points.at(i), (a.points.at(i) + a.points.at(i + 1)) / 2);
      path.lineTo(a.points.last());
    }
    break;
  default:
    break;
  }
  return path;
}

QColor contrastColor(const QColor &color, int alpha)
{
  return color.lightness() > 165 ? QColor(0, 0, 0, alpha) : QColor(255, 255, 255, alpha);
}

void paintArrow(QPainter &p, const Annotation &a)
{
  const QLineF line(a.start, a.end);
  const qreal length = line.length();
  if (length < 1)
    return;
  const QPointF dir = (a.end - a.start) / length;
  const QPointF normal(-dir.y(), dir.x());
  const qreal head = qMin(arrowHeadLength(a.size), length * 0.8);
  const QPointF base = a.end - dir * head;
  const QPointF neck = a.end - dir * head * 0.78;

  // A shaft that widens towards the head, then the head itself.
  QPolygonF shape;
  shape << a.start + normal * a.size * 0.3 << neck + normal * a.size * 0.65 << base + normal * head * 0.5 << a.end
        << base - normal * head * 0.5 << neck - normal * a.size * 0.65 << a.start - normal * a.size * 0.3;
  p.setPen(QPen(a.color, qMax(1.0, a.size * 0.25), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(a.color);
  p.drawPolygon(shape);
}

void paintMosaic(QPainter &p, const Annotation &a, const QImage &base)
{
  const QRect area = a.rect().toAlignedRect() & base.rect();
  if (area.isEmpty())
    return;
  if (a.mosaicCacheRect != area || a.mosaicCache.isNull()) {
    const int block = qMax(6, qRound(a.size * 2.5));
    const QSize small(qMax(1, area.width() / block), qMax(1, area.height() / block));
    a.mosaicCache = base.copy(area)
                        .scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                        .scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    a.mosaicCacheRect = area;
  }
  p.drawImage(area.topLeft(), a.mosaicCache);
}

void paintText(QPainter &p, const Annotation &a)
{
  const QFont font = textFont(a.size);
  const QFontMetricsF metrics(font);
  QPainterPath path;
  const auto lines = a.text.split(QLatin1Char('\n'));
  for (qsizetype i = 0; i < lines.size(); ++i)
    path.addText(a.start + QPointF(0, metrics.ascent() + i * metrics.lineSpacing()), font, lines.at(i));
  // A soft outline keeps the text readable on any background.
  p.setPen(QPen(contrastColor(a.color, 200), qMax(1.5, a.size * 0.14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  p.drawPath(path);
  p.setPen(Qt::NoPen);
  p.setBrush(a.color);
  p.drawPath(path);
}

void paintCounter(QPainter &p, const Annotation &a)
{
  const qreal radius = a.size;
  p.setPen(QPen(contrastColor(a.color, 230), qMax(1.5, radius * 0.14)));
  p.setBrush(a.color);
  p.drawEllipse(a.start, radius, radius);

  QFont font = textFont(radius * (a.text.size() > 1 ? 0.95 : 1.15));
  font.setWeight(QFont::Bold);
  p.setFont(font);
  p.setPen(a.color.lightness() > 165 ? Qt::black : Qt::white);
  p.drawText(QRectF(a.start - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)), Qt::AlignCenter, a.text);
}

} // namespace

QRectF Annotation::bounds() const
{
  switch (tool) {
  case Tool::Pen: {
    if (points.isEmpty())
      return {};
    QRectF box(points.first(), QSizeF(0, 0));
    for (const auto &point : points)
      box = box.united(QRectF(point, QSizeF(0, 0)));
    return box.adjusted(-size, -size, size, size);
  }
  case Tool::Text: {
    const QFontMetricsF metrics(textFont(size));
    qreal width = 0;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const auto &line : lines)
      width = qMax(width, metrics.horizontalAdvance(line));
    return QRectF(start, QSizeF(width, metrics.lineSpacing() * lines.size())).adjusted(-2, -2, 2, 2);
  }
  case Tool::Counter:
    return QRectF(start - QPointF(size, size), QSizeF(2 * size, 2 * size)).adjusted(-2, -2, 2, 2);
  case Tool::Arrow: {
    const qreal pad = arrowHeadLength(size) * 0.5;
    return rect().adjusted(-pad, -pad, pad, pad);
  }
  case Tool::Highlight:
  case Tool::Mosaic:
    return rect();
  default:
    return rect().adjusted(-size, -size, size, size);
  }
}

bool Annotation::contains(const QPointF &pos, qreal tolerance) const
{
  switch (tool) {
  case Tool::Rectangle:
  case Tool::Ellipse:
  case Tool::Line:
  case Tool::Arrow:
  case Tool::Pen: {
    QPainterPathStroker stroker;
    stroker.setWidth(size + 2 * tolerance);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    if (stroker.createStroke(strokeShape(*this)).contains(pos))
      return true;
    // The arrow head is wider than the shaft.
    return tool == Tool::Arrow && QLineF(pos, end).length() < arrowHeadLength(size) * 0.7 + tolerance;
  }
  default:
    return bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(pos);
  }
}

void Annotation::translate(const QPointF &offset)
{
  start += offset;
  end += offset;
  for (auto &point : points)
    point += offset;
}

bool Annotation::isEmpty() const
{
  switch (tool) {
  case Tool::Pen:
    return points.size() < 2;
  case Tool::Text:
    return text.trimmed().isEmpty();
  case Tool::Counter:
    return false;
  case Tool::Line:
  case Tool::Arrow:
    return QLineF(start, end).length() < 4;
  default:
    return rect().width() < 3 || rect().height() < 3;
  }
}

void Annotation::paint(QPainter &p, const QImage &base) const
{
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  switch (tool) {
  case Tool::Rectangle:
  case Tool::Ellipse:
  case Tool::Line:
  case Tool::Pen:
    p.setPen(QPen(color, size, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (tool == Tool::Rectangle)
      p.drawRoundedRect(rect(), size * 0.6, size * 0.6);
    else
      p.drawPath(strokeShape(*this));
    break;
  case Tool::Arrow:
    paintArrow(p, *this);
    break;
  case Tool::Highlight: {
    // Like a marker pen: multiply a light tint so the text underneath stays dark.
    const QColor tint((color.red() + 255) / 2, (color.green() + 255) / 2, (color.blue() + 255) / 2);
    p.setCompositionMode(QPainter::CompositionMode_Multiply);
    p.fillRect(rect(), tint);
    break;
  }
  case Tool::Mosaic:
    paintMosaic(p, *this, base);
    break;
  case Tool::Text:
    paintText(p, *this);
    break;
  case Tool::Counter:
    paintCounter(p, *this);
    break;
  case Tool::Select:
  case Tool::Crop:
    break;
  }
  p.restore();
}

QImage renderAnnotated(const QImage &base, const QList<Annotation> &annotations, const QRectF &crop)
{
  QImage source = base;
  source.setDevicePixelRatio(1);
  const QRect area = (crop.isEmpty() ? QRectF(source.rect()) : crop).toAlignedRect() & source.rect();
  QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  QPainter p(&out);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.translate(-area.topLeft());
  p.drawImage(QPointF(0, 0), source);
  for (const auto &annotation : annotations)
    annotation.paint(p, source);
  p.end();
  return out;
}

} // namespace deskflow::gui::screenshot
