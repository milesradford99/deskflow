/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QWidget>

namespace deskflow::gui::screenshot {

/**
 * @brief Freezes the screens and lets the user drag out the area to capture.
 *
 * Used where the system has no interactive capture of its own (Windows, X11);
 * macOS uses its own screencapture tool instead.
 */
class RegionSelector : public QWidget
{
  Q_OBJECT
public:
  explicit RegionSelector(QWidget *parent = nullptr);

  //! Grab every screen and cover them. False when the screens can't be read (e.g. on Wayland).
  bool start();
  //! For previews and tests: use @p desktop as the frozen screens instead of grabbing them.
  void startWith(const QImage &desktop, const QRect &geometry);
  //! For previews: pretend the user dragged out @p selection (widget coordinates).
  void setSelection(const QRect &selection);

Q_SIGNALS:
  void captured(const QImage &image);
  void cancelled();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  void cover();
  QImage crop(const QRect &area) const;
  void finish(const QRect &area);

  QImage m_desktop;
  qreal m_ratio = 1;
  QRect m_selection;
  QPoint m_press;
  bool m_dragging = false;
};

} // namespace deskflow::gui::screenshot
