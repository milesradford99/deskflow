/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class QPainter;
class QVariantAnimation;

/**
 * @brief Brief animation around the cursor when it arrives on this computer.
 *
 * A soft glow blooms under the cursor while two thin rings ripple outwards and fade,
 * all within about 0.7 s. The window is click-through, never takes focus, follows the
 * cursor while it plays and is hidden (with its animation stopped) as soon as it ends,
 * so it costs nothing in between.
 */
class CursorLocator : public QWidget
{
  Q_OBJECT

public:
  explicit CursorLocator(QWidget *parent = nullptr);

  //! Play the animation around the current cursor position.
  void locate();

  //! Draws one frame centred on \c center; \c progress runs from 0 to 1.
  static void paintFrame(QPainter &painter, const QPointF &center, qreal progress);

  static constexpr int kDurationMs = 720;
  static constexpr int kSize = 220;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  void followCursor();

  QVariantAnimation *m_animation;
  qreal m_progress = 0;
};
