/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QPointer>
#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace deskflow::gui::screenshot {

/**
 * @brief The floating preview that slides in at the bottom right after a capture.
 *
 * Like the macOS screenshot thumbnail: it waits a few seconds (longer while
 * the pointer is over it) and slides away. Click it to edit, drag it into
 * another app to drop the picture as a file, or use its close and save buttons.
 */
class ScreenshotThumbnail : public QWidget
{
  Q_OBJECT
public:
  explicit ScreenshotThumbnail(QWidget *parent = nullptr);

  //! Show @p image near the bottom right of @p screen, replacing any previous one.
  void present(const QImage &image, QScreen *screen, int seconds);
  //! Slide away now.
  void dismiss();
  //! Hide at once, e.g. before the next capture so it isn't in the picture.
  void hideNow();

  QImage image() const
  {
    return m_image;
  }

  //! For previews: draw as if the pointer were over it.
  void setHovered(bool hovered);

Q_SIGNALS:
  void editRequested(const QImage &image);
  void saveRequested(const QImage &image);

protected:
  void paintEvent(QPaintEvent *event) override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  QRectF imageRect() const;
  QRectF closeButtonRect() const;
  QRectF saveButtonRect() const;
  void startDrag();
  void restartTimer(int msec);

  QImage m_image;
  QPixmap m_preview;
  QTimer *m_timer;
  QPropertyAnimation *m_slide;
  QPoint m_restPos;
  int m_seconds = 5;
  bool m_hover = false;
  bool m_pressed = false;
  bool m_dismissing = false;
  QPoint m_pressPos;
};

} // namespace deskflow::gui::screenshot
