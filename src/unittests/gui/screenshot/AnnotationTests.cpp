/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AnnotationTests.h"

#include "gui/screenshot/Annotation.h"
#include "gui/screenshot/AnnotationCanvas.h"

#include <QPainter>

using namespace deskflow::gui::screenshot;

namespace {

QImage whiteImage(int width = 100, int height = 80)
{
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  return image;
}

Annotation shape(Tool tool, QPointF start, QPointF end, qreal size = 4)
{
  Annotation a;
  a.tool = tool;
  a.color = Qt::red;
  a.size = size;
  a.start = start;
  a.end = end;
  return a;
}

} // namespace

void AnnotationTests::renderCropsAndFlattens()
{
  const auto rect = shape(Tool::Rectangle, {20, 20}, {50, 40});
  const auto out = renderAnnotated(whiteImage(), {rect}, QRectF(10, 10, 50, 40));
  QCOMPARE(out.size(), QSize(50, 40));
  // The rectangle's top edge, 10 px into the crop.
  QCOMPARE(QColor(out.pixel(25, 10)).red(), 255);
  QVERIFY(QColor(out.pixel(25, 10)).green() < 80);
  // Inside the rectangle stays white.
  QCOMPARE(QColor(out.pixel(25, 20)), QColor(Qt::white));

  // An empty crop means the whole picture; a crop past the edge is clipped.
  QCOMPARE(renderAnnotated(whiteImage(), {}, QRectF()).size(), QSize(100, 80));
  QCOMPARE(renderAnnotated(whiteImage(), {}, QRectF(90, 70, 50, 50)).size(), QSize(10, 10));
}

void AnnotationTests::emptyMarks()
{
  QVERIFY(shape(Tool::Rectangle, {10, 10}, {11, 11}).isEmpty());
  QVERIFY(shape(Tool::Arrow, {10, 10}, {12, 11}).isEmpty());
  QVERIFY(!shape(Tool::Arrow, {10, 10}, {40, 10}).isEmpty());

  auto counter = shape(Tool::Counter, {10, 10}, {10, 10});
  counter.text = QStringLiteral("1");
  QVERIFY(!counter.isEmpty());

  auto text = shape(Tool::Text, {10, 10}, {10, 10});
  text.text = QStringLiteral("   ");
  QVERIFY(text.isEmpty());
  text.text = QStringLiteral("Hi");
  QVERIFY(!text.isEmpty());

  auto pen = shape(Tool::Pen, {10, 10}, {10, 10});
  pen.points = {QPointF(10, 10)};
  QVERIFY(pen.isEmpty());
}

void AnnotationTests::hitTesting()
{
  const auto line = shape(Tool::Line, {0, 0}, {100, 0});
  QVERIFY(line.contains({50, 1}, 2));
  QVERIFY(!line.contains({50, 20}, 2));

  // A rectangle is hit on its outline, not in its middle.
  auto rect = shape(Tool::Rectangle, {10, 10}, {90, 70});
  QVERIFY(rect.contains({10, 40}, 2));
  QVERIFY(!rect.contains({50, 40}, 2));

  // Filled marks are hit anywhere inside.
  QVERIFY(shape(Tool::Highlight, {10, 10}, {90, 70}).contains({50, 40}, 2));

  rect.translate({5, 5});
  QCOMPARE(rect.rect(), QRectF(15, 15, 80, 60));
}

void AnnotationTests::mosaicPixelates()
{
  QImage base(64, 64, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      base.setPixel(x, y, qRgb(x * 4, y * 4, (x * y) % 256));
  const auto mosaic = shape(Tool::Mosaic, {0, 0}, {64, 64}, 8); // 20 px blocks
  const auto out = renderAnnotated(base, {mosaic}, QRectF());
  // Neighbouring pixels in one block are the same colour; the original was a gradient.
  QCOMPARE(out.pixel(2, 2), out.pixel(3, 3));
  QVERIFY(base.pixel(2, 2) != base.pixel(3, 3));
}

void AnnotationTests::canvasUndoRedo()
{
  AnnotationCanvas canvas;
  auto image = whiteImage(200, 100);
  image.setDevicePixelRatio(2);
  canvas.setImage(image);
  QVERIFY(!canvas.canUndo());
  QCOMPARE(canvas.resultSize(), QSize(200, 100));

  canvas.addAnnotation(shape(Tool::Rectangle, {20, 20}, {60, 60}));
  QVERIFY(canvas.isModified());
  QVERIFY(canvas.result().pixel(20, 40) != qRgb(255, 255, 255));
  // The result keeps the screenshot's pixel ratio.
  QCOMPARE(canvas.result().devicePixelRatio(), 2.0);

  canvas.undo();
  QVERIFY(!canvas.canUndo());
  QVERIFY(canvas.canRedo());
  QCOMPARE(canvas.result().pixel(20, 40), qRgb(255, 255, 255));

  canvas.redo();
  QVERIFY(canvas.result().pixel(20, 40) != qRgb(255, 255, 255));
  QVERIFY(!canvas.deleteSelection()); // nothing selected
}

QTEST_MAIN(AnnotationTests)
