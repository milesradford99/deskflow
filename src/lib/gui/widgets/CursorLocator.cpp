/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CursorLocator.h"

#ifdef Q_OS_MACOS
#include "gui/OSXHelpers.h"
#endif

#include <QCursor>
#include <QEasingCurve>
#include <QPainter>
#include <QRadialGradient>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace {

// A calm blue that reads on both light and dark backgrounds, with white highlights.
const QColor kAccent(0x4d, 0xa3, 0xff);

qreal clamp01(qreal v)
{
  return std::clamp<qreal>(v, 0, 1);
}

//! Progress of a phase that runs from \c start to \c end of the whole animation.
qreal phase(qreal t, qreal start, qreal end)
{
  return clamp01((t - start) / (end - start));
}

qreal easeOut(qreal t)
{
  return 1 - std::pow(1 - t, 3);
}

QColor withAlpha(QColor color, qreal alpha)
{
  color.setAlphaF(clamp01(alpha));
  return color;
}

void paintRing(QPainter &painter, const QPointF &center, qreal t)
{
  if (t <= 0 || t >= 1)
    return;

  const qreal radius = 12 + 86 * easeOut(t);
  // fades in over the first few frames, then out as it travels
  const qreal alpha = std::min(t * 6, 1.0) * std::pow(1 - t, 1.6);

  painter.setBrush(Qt::NoBrush);
  // soft coloured halo, then a crisp line on top
  painter.setPen(QPen(withAlpha(kAccent, 0.28 * alpha), 7));
  painter.drawEllipse(center, radius, radius);
  painter.setPen(QPen(withAlpha(kAccent.lighter(125), 0.95 * alpha), 2.2));
  painter.drawEllipse(center, radius, radius);
  // thin dark edge so it still shows on white
  painter.setPen(QPen(withAlpha(Qt::black, 0.12 * alpha), 1));
  painter.drawEllipse(center, radius + 2.2, radius + 2.2);
}

} // namespace

CursorLocator::CursorLocator(QWidget *parent)
    : QWidget(
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                      Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint
      ),
      m_animation{new QVariantAnimation(this)}
{
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_MacAlwaysShowToolWindow);
  setFocusPolicy(Qt::NoFocus);
  setWindowTitle(QStringLiteral("Deskflow cursor"));
  resize(kSize, kSize);

  m_animation->setStartValue(0.0);
  m_animation->setEndValue(1.0);
  m_animation->setDuration(kDurationMs);
  connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
    m_progress = value.toReal();
    followCursor();
    update();
  });
  connect(m_animation, &QVariantAnimation::finished, this, &QWidget::hide);
}

void CursorLocator::locate()
{
  m_animation->stop();
  m_progress = 0;
  followCursor();
  show();
#ifdef Q_OS_MACOS
  // never raise(): on macOS that activates the app and brings the main window forward
  macOSOrderFrontWithoutActivating(this);
#else
  raise();
#endif
  m_animation->start();
}

void CursorLocator::followCursor()
{
  const auto pos = QCursor::pos();
  const QPoint topLeft(pos.x() - kSize / 2, pos.y() - kSize / 2);
  if (topLeft != geometry().topLeft())
    move(topLeft);
}

void CursorLocator::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  paintFrame(painter, QPointF(width() / 2.0, height() / 2.0), m_progress);
}

void CursorLocator::paintFrame(QPainter &painter, const QPointF &center, qreal t)
{
  painter.setRenderHint(QPainter::Antialiasing);

  // glow: blooms quickly, lingers a moment, fades
  const qreal bloom = easeOut(phase(t, 0, 0.3));
  const qreal fade = 1 - phase(t, 0.45, 1);
  const qreal glowAlpha = bloom * fade * fade;
  if (glowAlpha > 0) {
    const qreal radius = 16 + 22 * bloom;
    QRadialGradient glow(center, radius);
    glow.setColorAt(0.0, withAlpha(Qt::white, 0.85 * glowAlpha));
    glow.setColorAt(0.35, withAlpha(kAccent.lighter(130), 0.55 * glowAlpha));
    glow.setColorAt(1.0, withAlpha(kAccent, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(center, radius, radius);
  }

  // two rings, the second a little behind the first
  paintRing(painter, center, phase(t, 0.0, 0.8));
  paintRing(painter, center, phase(t, 0.18, 1.0));
}
