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

// Look: a neutral "focus lens" in the spirit of macOS pointer feedback (shake to locate,
// the camera focus square, Find My's target lock). A white ring with a soft shadow reads
// on any wallpaper, light or dark, and a faint iris tint ties it to the app.
const QColor kTint(0x5a, 0x55, 0xd6);

constexpr qreal kStartRadius = 64; // where the lens appears
constexpr qreal kRestRadius = 24;  // where it settles around the pointer
constexpr qreal kShadow = 9;       // reach of the soft shadow beyond the ring

qreal clamp01(qreal v)
{
  return std::clamp<qreal>(v, 0, 1);
}

//! Progress of a phase that runs from \c start to \c end of the whole animation.
qreal phase(qreal t, qreal start, qreal end)
{
  return clamp01((t - start) / (end - start));
}

qreal easeOutCubic(qreal t)
{
  return 1 - std::pow(1 - t, 3);
}

qreal easeInCubic(qreal t)
{
  return t * t * t;
}

//! Ease-out with a small overshoot, so the lens "snaps" onto the pointer like a spring.
qreal easeOutBack(qreal t)
{
  constexpr qreal s = 1.35;
  const qreal u = t - 1;
  return 1 + (s + 1) * u * u * u + s * u * u;
}

QColor withAlpha(QColor color, qreal alpha)
{
  color.setAlphaF(clamp01(alpha));
  return color;
}

//! Soft, slightly lowered shadow hugging a ring of \c radius, like a macOS window shadow.
void paintShadow(QPainter &painter, const QPointF &center, qreal radius, qreal alpha)
{
  const qreal outer = radius + kShadow;
  QRadialGradient shadow(center + QPointF(0, 1.5), outer);
  const auto at = [outer](qreal r) { return clamp01(r / outer); };
  shadow.setColorAt(0, withAlpha(Qt::black, 0));
  shadow.setColorAt(at(radius - kShadow * 0.6), withAlpha(Qt::black, 0));
  shadow.setColorAt(at(radius), withAlpha(Qt::black, 0.26 * alpha));
  shadow.setColorAt(1, withAlpha(Qt::black, 0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(shadow);
  painter.drawEllipse(center + QPointF(0, 1.5), outer, outer);
}

void paintLens(QPainter &painter, const QPointF &center, qreal radius, qreal alpha)
{
  if (alpha <= 0)
    return;

  paintShadow(painter, center, radius, alpha);

  // barely-there glass: lifts dark backgrounds a touch, tinted with the app's iris
  painter.setPen(Qt::NoPen);
  painter.setBrush(withAlpha(Qt::white, 0.06 * alpha));
  painter.drawEllipse(center, radius, radius);
  painter.setBrush(withAlpha(kTint, 0.06 * alpha));
  painter.drawEllipse(center, radius, radius);

  // the ring itself, with a hairline inside so it still has an edge on white
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(withAlpha(Qt::black, 0.12 * alpha), 1));
  painter.drawEllipse(center, radius - 2, radius - 2);
  painter.setPen(QPen(withAlpha(Qt::white, 0.97 * alpha), 2.5));
  painter.drawEllipse(center, radius, radius);
}

//! One quiet ring that leaves the lens as it lands, confirming the arrival.
void paintEcho(QPainter &painter, const QPointF &center, qreal t)
{
  if (t <= 0 || t >= 1)
    return;
  const qreal radius = kRestRadius + 20 * easeOutCubic(t);
  const qreal alpha = 0.55 * std::pow(1 - t, 2);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(withAlpha(Qt::black, 0.18 * alpha), 3));
  painter.drawEllipse(center + QPointF(0, 1), radius, radius);
  painter.setPen(QPen(withAlpha(Qt::white, alpha), 1.25));
  painter.drawEllipse(center, radius, radius);
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

  // 1. the lens appears wide and closes in on the pointer, snapping into place
  const qreal approach = phase(t, 0, 0.42);
  qreal radius = kStartRadius + (kRestRadius - kStartRadius) * easeOutBack(approach);
  qreal alpha = easeOutCubic(clamp01(approach * 3));

  // 2. it rests around the pointer for a beat, then 3. shrinks a little as it fades
  const qreal exit = easeInCubic(phase(t, 0.66, 1));
  radius *= 1 - 0.18 * exit;
  alpha *= 1 - exit;

  paintEcho(painter, center, phase(t, 0.34, 0.84));
  paintLens(painter, center, radius, alpha);
}
