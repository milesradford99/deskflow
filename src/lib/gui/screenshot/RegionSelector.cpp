/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RegionSelector.h"

#include "gui/Theme.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

namespace deskflow::gui::screenshot {

RegionSelector::RegionSelector(QWidget *parent)
    : QWidget(
          parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint |
                      Qt::NoDropShadowWindowHint
      )
{
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  setCursor(Qt::CrossCursor);
}

bool RegionSelector::start()
{
  QRect virtualGeometry;
  qreal ratio = 1;
  const auto screens = QGuiApplication::screens();
  for (const auto *screen : screens) {
    virtualGeometry = virtualGeometry.united(screen->geometry());
    ratio = qMax(ratio, screen->devicePixelRatio());
  }
  if (virtualGeometry.isEmpty())
    return false;

  QImage desktop((QSizeF(virtualGeometry.size()) * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
  desktop.fill(Qt::black);
  QPainter p(&desktop);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  bool grabbed = false;
  for (auto *screen : screens) {
    const QPixmap shot = screen->grabWindow(0);
    if (shot.isNull())
      continue;
    grabbed = true;
    const QRect geometry = screen->geometry().translated(-virtualGeometry.topLeft());
    p.drawPixmap(QRectF(QPointF(geometry.topLeft()) * ratio, QSizeF(geometry.size()) * ratio), shot, shot.rect());
  }
  p.end();
  if (!grabbed)
    return false;

  desktop.setDevicePixelRatio(ratio);
  startWith(desktop, virtualGeometry);
  return true;
}

void RegionSelector::startWith(const QImage &desktop, const QRect &geometry)
{
  m_desktop = desktop;
  m_ratio = qMax(1.0, desktop.devicePixelRatio());
  m_desktop.setDevicePixelRatio(1);
  setGeometry(geometry);
  cover();
}

void RegionSelector::cover()
{
  show();
  raise();
  activateWindow();
  setFocus(Qt::OtherFocusReason);
  grabKeyboard();
}

void RegionSelector::setSelection(const QRect &selection)
{
  m_selection = selection.normalized();
  update();
}

void RegionSelector::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawImage(QRectF(rect()), m_desktop);

  QPainterPath dim;
  dim.addRect(rect());
  if (!m_selection.isEmpty()) {
    QPainterPath hole;
    hole.addRect(m_selection);
    dim = dim.subtracted(hole);
  }
  p.fillPath(dim, QColor(0, 0, 0, 105));

  p.setRenderHint(QPainter::Antialiasing);
  QFont font = this->font();
  font.setPixelSize(12);
  font.setWeight(QFont::DemiBold);
  p.setFont(font);
  const QFontMetrics metrics(font);

  if (!m_selection.isEmpty()) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(QRectF(m_selection).adjusted(-0.5, -0.5, 0.5, 0.5));

    // Size in real pixels, above the selection (or inside it at the top of the screen).
    const QString text = QStringLiteral("%1 × %2")
                             .arg(qRound(m_selection.width() * m_ratio))
                             .arg(qRound(m_selection.height() * m_ratio));
    QRectF badge(0, 0, metrics.horizontalAdvance(text) + 16, metrics.height() + 8);
    badge.moveBottomLeft(QPointF(m_selection.left(), m_selection.top() - 6));
    if (badge.top() < 4)
      badge.moveTopLeft(QPointF(m_selection.left() + 6, m_selection.top() + 6));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 175));
    p.drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, text);
  }

  if (!m_dragging) {
    // A short hint at the top of the screen with the pointer.
    const QString hint = tr("Drag to capture an area  ·  Click for the whole screen  ·  Esc to cancel");
    const auto *screen = QGuiApplication::screenAt(QCursor::pos());
    const QRect area = screen ? screen->geometry().translated(-geometry().topLeft()) : rect();
    QRectF pill(0, 0, metrics.horizontalAdvance(hint) + 32, metrics.height() + 16);
    pill.moveCenter(QPointF(area.center().x(), area.top() + 28 + pill.height() / 2));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(28, 28, 30, 215));
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(pill, Qt::AlignCenter, hint);
  }
}

void RegionSelector::mousePressEvent(QMouseEvent *event)
{
  if (event->button() == Qt::RightButton) {
    releaseKeyboard();
    Q_EMIT cancelled();
    close();
    return;
  }
  if (event->button() != Qt::LeftButton)
    return;
  m_press = event->position().toPoint();
  m_selection = QRect(m_press, QSize());
  m_dragging = true;
  update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event)
{
  if (!m_dragging)
    return;
  m_selection = QRect(m_press, event->position().toPoint()).normalized() & rect();
  update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton || !m_dragging)
    return;
  m_dragging = false;
  QRect area = m_selection;
  if (area.width() < 4 || area.height() < 4) {
    // A click: the whole screen under the pointer.
    const auto *screen = QGuiApplication::screenAt(mapToGlobal(event->position().toPoint()));
    area = screen ? screen->geometry().translated(-geometry().topLeft()) : rect();
  }
  finish(area);
}

void RegionSelector::keyPressEvent(QKeyEvent *event)
{
  switch (event->key()) {
  case Qt::Key_Escape:
    releaseKeyboard();
    Q_EMIT cancelled();
    close();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    finish(m_selection.isEmpty() ? rect() : m_selection);
    return;
  default:
    QWidget::keyPressEvent(event);
  }
}

QImage RegionSelector::crop(const QRect &area) const
{
  const QRect pixels = QRectF(QPointF(area.topLeft()) * m_ratio, QSizeF(area.size()) * m_ratio).toAlignedRect();
  QImage image = m_desktop.copy(pixels & m_desktop.rect());
  image.setDevicePixelRatio(m_ratio);
  return image;
}

void RegionSelector::finish(const QRect &area)
{
  releaseKeyboard();
  const QImage image = crop(area);
  hide();
  Q_EMIT captured(image);
  close();
}

} // namespace deskflow::gui::screenshot
