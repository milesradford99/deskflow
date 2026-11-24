/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace deskflow::gui::screenshot {

//! Editor tools, in toolbar order.
enum class Tool
{
  Select,
  Crop,
  Rectangle,
  Ellipse,
  Arrow,
  Line,
  Pen,
  Highlight,
  Text,
  Counter,
  Mosaic
};

/**
 * @brief One mark drawn on a screenshot, in image pixel coordinates.
 *
 * Shapes use the drag from @c start to @c end; the pen keeps its points;
 * text and step counters are anchored at @c start.
 */
struct Annotation
{
  Tool tool = Tool::Rectangle;
  QColor color = Qt::red;
  //! Stroke width, or the font / badge size for text and counters, in image pixels.
  qreal size = 4;
  QPointF start;
  QPointF end;
  QList<QPointF> points;
  QString text;

  // Pixelated copy of the screenshot for a mosaic, kept between paints.
  mutable QImage mosaicCache;
  mutable QRect mosaicCacheRect;

  //! The drag as a normalised rectangle.
  QRectF rect() const
  {
    return QRectF(start, end).normalized();
  }
  //! Everything the mark paints over, for hit testing and selection outlines.
  QRectF bounds() const;
  //! True when @p pos (image coordinates) is on the mark, within @p tolerance pixels.
  bool contains(const QPointF &pos, qreal tolerance) const;
  void translate(const QPointF &offset);
  //! A mark too small to keep, e.g. a click with a shape tool.
  bool isEmpty() const;

  //! Paint onto @p painter, which is in image coordinates; @p base is the screenshot, for the mosaic.
  void paint(QPainter &painter, const QImage &base) const;
};

//! The screenshot cropped to @p crop with @p annotations flattened onto it.
QImage renderAnnotated(const QImage &base, const QList<Annotation> &annotations, const QRectF &crop);

} // namespace deskflow::gui::screenshot
