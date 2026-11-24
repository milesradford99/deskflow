/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AnnotationCanvas.h"

#include "gui/Theme.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>

namespace deskflow::gui::screenshot {

namespace {

constexpr int kMargin = 28;
constexpr int kMaxUndo = 100;
constexpr qreal kHandleSize = 9;
constexpr qreal kMinCrop = 8;

// Sizes in points; multiplied by the screenshot's pixel ratio.
constexpr qreal kStroke[] = {3, 5, 9};
constexpr qreal kFont[] = {18, 26, 40};
constexpr qreal kCounter[] = {11, 14, 18};
constexpr qreal kMosaic[] = {4, 6, 10};

bool isDragShape(Tool tool)
{
  switch (tool) {
  case Tool::Rectangle:
  case Tool::Ellipse:
  case Tool::Arrow:
  case Tool::Line:
  case Tool::Pen:
  case Tool::Highlight:
  case Tool::Mosaic:
    return true;
  default:
    return false;
  }
}

//! Shift: squares and circles, lines at multiples of 45 degrees.
QPointF constrained(Tool tool, const QPointF &start, const QPointF &end)
{
  const QPointF d = end - start;
  if (tool == Tool::Line || tool == Tool::Arrow) {
    const qreal length = qSqrt(d.x() * d.x() + d.y() * d.y());
    const qreal angle = qRound(qAtan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4);
    return start + QPointF(qCos(angle), qSin(angle)) * length;
  }
  const qreal side = qMax(qAbs(d.x()), qAbs(d.y()));
  return start + QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
}

} // namespace

AnnotationCanvas::AnnotationCanvas(QWidget *parent) : QWidget(parent), m_color(QColor(0xFF, 0x3B, 0x30))
{
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(320, 220);
}

void AnnotationCanvas::setImage(const QImage &image)
{
  commitText();
  m_dpr = qMax(1.0, image.devicePixelRatio());
  m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  m_image.setDevicePixelRatio(1);
  m_pixmap = QPixmap::fromImage(m_image);
  m_annotations.clear();
  m_crop = QRectF(m_image.rect());
  m_undo.clear();
  m_redo.clear();
  m_selected = -1;
  update();
  Q_EMIT changed();
}

QImage AnnotationCanvas::result() const
{
  auto out = renderAnnotated(m_image, m_annotations, m_crop);
  out.setDevicePixelRatio(m_dpr);
  return out;
}

QSize AnnotationCanvas::resultSize() const
{
  return (m_crop.toAlignedRect() & m_image.rect()).size();
}

void AnnotationCanvas::setTool(Tool tool)
{
  commitText();
  m_tool = tool;
  if (tool != Tool::Select)
    m_selected = -1;
  m_drag = Drag::None;
  updateCursor(mapFromGlobal(QCursor::pos()));
  update();
}

void AnnotationCanvas::setColor(const QColor &color)
{
  m_color = color;
  if (m_textEditor) {
    m_textColor = color;
    positionTextEditor();
  }
  if (m_selected >= 0 && m_annotations.at(m_selected).tool != Tool::Mosaic) {
    pushUndo();
    m_annotations[m_selected].color = color;
    update();
    Q_EMIT changed();
  }
}

void AnnotationCanvas::setStrokeLevel(int level)
{
  m_level = qBound(0, level, 2);
  if (m_textEditor) {
    m_textSize = sizeForTool(Tool::Text);
    positionTextEditor();
  }
  if (m_selected >= 0) {
    pushUndo();
    auto &annotation = m_annotations[m_selected];
    annotation.size = sizeForTool(annotation.tool);
    annotation.mosaicCache = QImage();
    update();
    Q_EMIT changed();
  }
}

qreal AnnotationCanvas::sizeForTool(Tool tool) const
{
  switch (tool) {
  case Tool::Text:
    return kFont[m_level] * m_dpr;
  case Tool::Counter:
    return kCounter[m_level] * m_dpr;
  case Tool::Mosaic:
    return kMosaic[m_level] * m_dpr;
  default:
    return kStroke[m_level] * m_dpr;
  }
}

AnnotationCanvas::State AnnotationCanvas::state() const
{
  return {m_annotations, m_crop};
}

void AnnotationCanvas::pushUndo()
{
  m_undo.append(state());
  if (m_undo.size() > kMaxUndo)
    m_undo.removeFirst();
  m_redo.clear();
}

void AnnotationCanvas::restore(const State &state)
{
  m_annotations = state.annotations;
  m_crop = state.crop;
  m_selected = -1;
  update();
  Q_EMIT changed();
}

void AnnotationCanvas::undo()
{
  commitText();
  if (m_undo.isEmpty())
    return;
  m_redo.append(state());
  restore(m_undo.takeLast());
}

void AnnotationCanvas::redo()
{
  commitText();
  if (m_redo.isEmpty())
    return;
  m_undo.append(state());
  restore(m_redo.takeLast());
}

bool AnnotationCanvas::deleteSelection()
{
  if (m_selected < 0 || m_selected >= m_annotations.size())
    return false;
  pushUndo();
  m_annotations.removeAt(m_selected);
  m_selected = -1;
  update();
  Q_EMIT changed();
  return true;
}

void AnnotationCanvas::resetCrop()
{
  if (m_crop == QRectF(m_image.rect()))
    return;
  pushUndo();
  m_crop = QRectF(m_image.rect());
  update();
  Q_EMIT changed();
}

void AnnotationCanvas::addAnnotation(const Annotation &annotation)
{
  pushUndo();
  m_annotations.append(annotation);
  update();
  Q_EMIT changed();
}

int AnnotationCanvas::nextCounter() const
{
  int highest = 0;
  for (const auto &annotation : m_annotations) {
    if (annotation.tool == Tool::Counter)
      highest = qMax(highest, annotation.text.toInt());
  }
  return highest + 1;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRectF AnnotationCanvas::framedRect() const
{
  return m_tool == Tool::Crop || m_crop.isEmpty() ? QRectF(m_image.rect()) : m_crop;
}

qreal AnnotationCanvas::viewScale() const
{
  const QRectF framed = framedRect();
  if (framed.isEmpty())
    return 1;
  const QRectF available = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
  // Never larger than the screenshot's own size on screen.
  return std::min({available.width() / framed.width(), available.height() / framed.height(), 1.0 / m_dpr});
}

QTransform AnnotationCanvas::imageToView() const
{
  const QRectF framed = framedRect();
  const qreal scale = viewScale();
  const QPointF centre = QRectF(rect()).center();
  QTransform transform;
  transform.translate(
      qRound(centre.x() - framed.width() * scale / 2), qRound(centre.y() - framed.height() * scale / 2)
  );
  transform.scale(scale, scale);
  transform.translate(-framed.left(), -framed.top());
  return transform;
}

QPointF AnnotationCanvas::toImage(const QPointF &viewPos) const
{
  return imageToView().inverted().map(viewPos);
}

int AnnotationCanvas::hitAnnotation(const QPointF &imagePos) const
{
  const qreal tolerance = 4 / viewScale();
  for (auto i = m_annotations.size() - 1; i >= 0; --i) {
    if (m_annotations.at(i).contains(imagePos, tolerance))
      return int(i);
  }
  return -1;
}

QList<QPointF> AnnotationCanvas::cropHandles() const
{
  const QRectF r = imageToView().mapRect(m_crop);
  const QPointF c = r.center();
  return {r.topLeft(),     QPointF(c.x(), r.top()),    r.topRight(),   QPointF(r.right(), c.y()),
          r.bottomRight(), QPointF(c.x(), r.bottom()), r.bottomLeft(), QPointF(r.left(), c.y())};
}

int AnnotationCanvas::cropHandleAt(const QPointF &viewPos) const
{
  const auto handles = cropHandles();
  for (qsizetype i = 0; i < handles.size(); ++i) {
    if (QLineF(handles.at(i), viewPos).length() <= kHandleSize)
      return int(i);
  }
  return -1;
}

void AnnotationCanvas::updateCursor(const QPointF &viewPos)
{
  Qt::CursorShape shape = Qt::CrossCursor;
  switch (m_tool) {
  case Tool::Crop: {
    static const Qt::CursorShape resize[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor,   Qt::SizeBDiagCursor,
                                             Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                             Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    if (const int handle = cropHandleAt(viewPos); handle >= 0)
      shape = resize[handle];
    else if (imageToView().mapRect(m_crop).contains(viewPos))
      shape = Qt::SizeAllCursor;
    break;
  }
  case Tool::Select:
    shape = hitAnnotation(toImage(viewPos)) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor;
    break;
  case Tool::Text:
    shape = Qt::IBeamCursor;
    break;
  default:
    break;
  }
  setCursor(shape);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void AnnotationCanvas::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  const bool dark = theme::isDark();
  p.fillRect(rect(), dark ? QColor(0x1C, 0x1C, 0x1E) : QColor(0xEC, 0xEB, 0xE7));
  if (m_image.isNull())
    return;

  const QTransform transform = imageToView();
  const QRectF framed = framedRect();
  const QRectF target = transform.mapRect(framed);

  // A soft shadow lifts the screenshot off the background.
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  for (int i = 1; i <= 8; ++i) {
    p.setBrush(QColor(0, 0, 0, dark ? 22 : 9));
    p.drawRoundedRect(target.adjusted(-i, -i + 3, i, i + 3), i + 2, i + 2);
  }

  p.save();
  p.setClipRect(target);
  p.setTransform(transform);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawPixmap(QPointF(0, 0), m_pixmap);
  for (const auto &annotation : m_annotations)
    annotation.paint(p, m_image);
  if (m_drag == Drag::Draw)
    m_current.paint(p, m_image);
  p.restore();

  const QColor accent = theme::accent();

  if (m_selected >= 0 && m_selected < m_annotations.size()) {
    const QRectF box = transform.mapRect(m_annotations.at(m_selected).bounds()).adjusted(-3, -3, 3, 3);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 3));
    p.drawRoundedRect(box, 4, 4);
    p.setPen(QPen(accent, 1.5, Qt::DashLine));
    p.drawRoundedRect(box, 4, 4);
  }

  if (m_tool != Tool::Crop)
    return;

  // Dim everything outside the crop and draw its frame and handles.
  const QRectF crop = transform.mapRect(m_crop);
  QPainterPath outside;
  outside.addRect(target);
  QPainterPath inside;
  inside.addRect(crop);
  p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 120));

  if (m_drag == Drag::CropMove || m_drag == Drag::CropResize || m_drag == Drag::CropNew) {
    p.setPen(QPen(QColor(255, 255, 255, 110), 1));
    for (int i = 1; i < 3; ++i) {
      const qreal x = crop.left() + crop.width() * i / 3;
      const qreal y = crop.top() + crop.height() * i / 3;
      p.drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
      p.drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
    }
  }
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(Qt::white, 1.5));
  p.drawRect(crop);
  for (const auto &handle : cropHandles()) {
    const QRectF box(handle - QPointF(kHandleSize / 2, kHandleSize / 2), QSizeF(kHandleSize, kHandleSize));
    p.setPen(QPen(QColor(0, 0, 0, 60), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(box, 2.5, 2.5);
  }

  // Size badge under the crop.
  const QSize size = resultSize();
  const QString text = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
  QFont font = this->font();
  font.setPixelSize(11);
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  const QFontMetrics metrics(font);
  QRectF badge(0, 0, metrics.horizontalAdvance(text) + 16, metrics.height() + 6);
  badge.moveCenter(QPointF(crop.center().x(), crop.bottom() + badge.height() / 2 + 8));
  if (badge.bottom() > height() - 2)
    badge.moveBottom(crop.bottom() - 8);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 170));
  p.drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
  p.setPen(Qt::white);
  p.drawText(badge, Qt::AlignCenter, text);
}

// ---------------------------------------------------------------------------
// Mouse and keyboard
// ---------------------------------------------------------------------------

void AnnotationCanvas::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton || m_image.isNull())
    return QWidget::mousePressEvent(event);
  setFocus(Qt::MouseFocusReason);

  const bool wasTyping = m_textEditor;
  commitText();

  const QPointF pos = toImage(event->position());
  m_pressImage = pos;
  m_lastImage = pos;

  switch (m_tool) {
  case Tool::Crop: {
    pushUndo();
    m_pressCrop = m_crop;
    m_cropHandle = cropHandleAt(event->position());
    if (m_cropHandle >= 0)
      m_drag = Drag::CropResize;
    else if (m_crop.contains(pos) && m_crop != QRectF(m_image.rect()))
      m_drag = Drag::CropMove;
    else
      m_drag = Drag::CropNew;
    break;
  }
  case Tool::Select:
    m_selected = hitAnnotation(pos);
    if (m_selected >= 0) {
      pushUndo();
      m_drag = Drag::Move;
    }
    break;
  case Tool::Text: {
    if (wasTyping)
      break; // the click only closed the text box
    const int hit = hitAnnotation(pos);
    if (hit >= 0 && m_annotations.at(hit).tool == Tool::Text) {
      // Edit existing text in place.
      pushUndo();
      const auto annotation = m_annotations.takeAt(hit);
      m_textColor = annotation.color;
      m_textSize = annotation.size;
      m_textUndoPushed = true;
      beginText(annotation.start, annotation.text);
    } else {
      m_textColor = m_color;
      m_textSize = sizeForTool(Tool::Text);
      m_textUndoPushed = false;
      beginText(pos - QPointF(0, m_textSize * 0.6));
    }
    break;
  }
  case Tool::Counter: {
    Annotation counter;
    counter.tool = Tool::Counter;
    counter.color = m_color;
    counter.size = sizeForTool(Tool::Counter);
    counter.start = pos;
    counter.end = pos;
    counter.text = QString::number(nextCounter());
    addAnnotation(counter);
    break;
  }
  default:
    if (isDragShape(m_tool)) {
      m_current = Annotation();
      m_current.tool = m_tool;
      m_current.color = m_color;
      m_current.size = sizeForTool(m_tool);
      m_current.start = pos;
      m_current.end = pos;
      if (m_tool == Tool::Pen)
        m_current.points = {pos};
      m_drag = Drag::Draw;
    }
    break;
  }
  update();
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *event)
{
  const QPointF pos = toImage(event->position());
  const QRectF bounds(m_image.rect());

  switch (m_drag) {
  case Drag::None:
    updateCursor(event->position());
    return;
  case Drag::Draw:
    if (m_current.tool == Tool::Pen) {
      if (QLineF(m_current.points.last(), pos).length() * viewScale() >= 1.5)
        m_current.points.append(pos);
    } else {
      m_current.end =
          (event->modifiers() & Qt::ShiftModifier) ? constrained(m_current.tool, m_current.start, pos) : pos;
    }
    break;
  case Drag::Move:
    if (m_selected >= 0) {
      m_annotations[m_selected].translate(pos - m_lastImage);
      m_annotations[m_selected].mosaicCache = QImage();
    }
    break;
  case Drag::CropNew: {
    const QPointF from(
        qBound(bounds.left(), m_pressImage.x(), bounds.right()), qBound(bounds.top(), m_pressImage.y(), bounds.bottom())
    );
    const QPointF to(qBound(bounds.left(), pos.x(), bounds.right()), qBound(bounds.top(), pos.y(), bounds.bottom()));
    m_crop = QRectF(from, to).normalized();
    break;
  }
  case Drag::CropMove: {
    QRectF moved = m_pressCrop.translated(pos - m_pressImage);
    moved.moveLeft(qBound(bounds.left(), moved.left(), bounds.right() - moved.width()));
    moved.moveTop(qBound(bounds.top(), moved.top(), bounds.bottom() - moved.height()));
    m_crop = moved;
    break;
  }
  case Drag::CropResize: {
    const QPointF d = pos - m_pressImage;
    qreal left = m_pressCrop.left(), top = m_pressCrop.top();
    qreal right = m_pressCrop.right(), bottom = m_pressCrop.bottom();
    const int h = m_cropHandle;
    if (h == 0 || h == 6 || h == 7)
      left = qBound(bounds.left(), left + d.x(), right - kMinCrop);
    if (h == 2 || h == 3 || h == 4)
      right = qBound(left + kMinCrop, right + d.x(), bounds.right());
    if (h == 0 || h == 1 || h == 2)
      top = qBound(bounds.top(), top + d.y(), bottom - kMinCrop);
    if (h == 4 || h == 5 || h == 6)
      bottom = qBound(top + kMinCrop, bottom + d.y(), bounds.bottom());
    m_crop = QRectF(QPointF(left, top), QPointF(right, bottom));
    break;
  }
  }
  m_lastImage = pos;
  update();
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton)
    return QWidget::mouseReleaseEvent(event);

  const Drag drag = m_drag;
  m_drag = Drag::None;
  switch (drag) {
  case Drag::Draw:
    if (!m_current.isEmpty()) {
      pushUndo();
      m_annotations.append(m_current);
    }
    break;
  case Drag::Move:
    if (m_lastImage == m_pressImage)
      m_undo.removeLast();
    break;
  case Drag::CropNew:
  case Drag::CropMove:
  case Drag::CropResize:
    if (m_crop.width() < kMinCrop || m_crop.height() < kMinCrop)
      m_crop = m_pressCrop;
    m_crop = QRectF(m_crop.toAlignedRect() & m_image.rect());
    if (m_crop == m_pressCrop)
      m_undo.removeLast();
    break;
  case Drag::None:
    break;
  }
  updateCursor(event->position());
  update();
  Q_EMIT changed();
}

void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (m_tool == Tool::Crop && event->button() == Qt::LeftButton) {
    Q_EMIT cropFinished();
    return;
  }
  mousePressEvent(event);
}

void AnnotationCanvas::keyPressEvent(QKeyEvent *event)
{
  switch (event->key()) {
  case Qt::Key_Delete:
  case Qt::Key_Backspace:
    if (deleteSelection())
      return;
    break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (m_tool == Tool::Crop) {
      Q_EMIT cropFinished();
      return;
    }
    break;
  case Qt::Key_Escape:
    if (m_drag != Drag::None) {
      m_drag = Drag::None;
      update();
      return;
    }
    if (m_selected >= 0) {
      m_selected = -1;
      update();
      return;
    }
    break;
  default:
    break;
  }
  QWidget::keyPressEvent(event);
}

void AnnotationCanvas::resizeEvent(QResizeEvent *event)
{
  QWidget::resizeEvent(event);
  positionTextEditor();
}

void AnnotationCanvas::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange)
    update();
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void AnnotationCanvas::beginText(const QPointF &imagePos, const QString &text)
{
  m_textAnchor = imagePos;
  auto *editor = new QLineEdit(this);
  editor->setFrame(false);
  editor->setText(text);
  editor->setAttribute(Qt::WA_MacShowFocusRect, false);
  editor->installEventFilter(this);
  connect(editor, &QLineEdit::textChanged, this, &AnnotationCanvas::positionTextEditor);
  m_textEditor = editor;
  positionTextEditor();
  editor->show();
  editor->setFocus(Qt::MouseFocusReason);
  update();
}

void AnnotationCanvas::positionTextEditor()
{
  if (!m_textEditor)
    return;
  const qreal scale = viewScale();
  QFont font;
  font.setPixelSize(qMax(8, qRound(m_textSize * scale)));
  font.setWeight(QFont::DemiBold);
  m_textEditor->setFont(font);
  m_textEditor->setStyleSheet(QStringLiteral(
                                  "QLineEdit { background: rgba(255,255,255,0.12); color: %1;"
                                  " border: 1px dashed %2; border-radius: 4px; padding: 0 2px; }"
  )
                                  .arg(m_textColor.name(), theme::accent().name()));
  const QFontMetrics metrics(font);
  const int width = metrics.horizontalAdvance(m_textEditor->text() + QStringLiteral("MM")) + 8;
  const QPointF topLeft = imageToView().map(m_textAnchor);
  m_textEditor->setGeometry(qRound(topLeft.x()) - 3, qRound(topLeft.y()) - 2, qMax(80, width), metrics.height() + 6);
}

void AnnotationCanvas::commitText()
{
  if (!m_textEditor)
    return;
  QPointer<QLineEdit> editor = m_textEditor;
  m_textEditor = nullptr;
  const QString text = editor->text();
  editor->hide();
  editor->deleteLater();

  if (!text.trimmed().isEmpty()) {
    if (!m_textUndoPushed)
      pushUndo();
    Annotation annotation;
    annotation.tool = Tool::Text;
    annotation.color = m_textColor;
    annotation.size = m_textSize;
    annotation.start = m_textAnchor;
    annotation.end = m_textAnchor;
    annotation.text = text;
    m_annotations.append(annotation);
  }
  m_textUndoPushed = false;
  setFocus(Qt::OtherFocusReason);
  update();
  Q_EMIT changed();
}

bool AnnotationCanvas::eventFilter(QObject *watched, QEvent *event)
{
  if (watched == m_textEditor) {
    if (event->type() == QEvent::KeyPress) {
      // QLineEdit passes Return on to its parent, where it would copy and close the editor.
      const int key = static_cast<QKeyEvent *>(event)->key();
      if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
        commitText();
        return true;
      }
    }
    if (event->type() == QEvent::FocusOut) {
      // Defer: the focus may be moving to a toolbar button that changes the colour first.
      QMetaObject::invokeMethod(
          this,
          [this] {
            if (m_textEditor && !m_textEditor->hasFocus() && QApplication::activePopupWidget() == nullptr)
              commitText();
          },
          Qt::QueuedConnection
      );
    }
  }
  return QWidget::eventFilter(watched, event);
}

} // namespace deskflow::gui::screenshot
