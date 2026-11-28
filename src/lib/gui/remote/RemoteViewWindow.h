/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "remoteview/RemoteFrame.h"

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;

namespace deskflow::gui::mac {
class SegmentedControl;
}

namespace deskflow::gui::remote {

//! Shows the picture and turns what happens over it into input for the client
class RemoteViewCanvas : public QWidget
{
  Q_OBJECT
public:
  explicit RemoteViewCanvas(QWidget *parent = nullptr);

  void setPicture(const QImage &picture, const QRect &source);
  void clearPicture();
  //! A centred notice over (or instead of) the picture; empty hides it
  void setNotice(const QString &title, const QString &detail = QString());
  //! Whether input goes to the client (false while only watching)
  void setInteractive(bool interactive);
  bool hasPicture() const
  {
    return !m_picture.isNull();
  }

Q_SIGNALS:
  //! One input event for the core: {t, x, y, ...} in the client's screen coordinates
  void input(const QJsonObject &event);

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void focusOutEvent(QFocusEvent *event) override;
  bool focusNextPrevChild(bool next) override;

private:
  QRectF pictureRect() const;
  //! Widget position to client coordinates; false outside the picture
  bool toRemote(const QPointF &position, QPoint &remote) const;
  void sendKey(QKeyEvent *event, bool down);
  void flushMove();

  QImage m_picture;
  QRect m_source;
  QString m_noticeTitle;
  QString m_noticeDetail;
  bool m_interactive = true;
  bool m_focused = false;
  QPoint m_lastRemote;
  bool m_hasLast = false;
  QPoint m_pendingMove;
  bool m_movePending = false;
  QTimer m_moveTimer;
  QPointF m_wheelRemainder;
};

//! A window showing one client's screen, live, and letting you use it
class RemoteViewWindow : public QWidget
{
  Q_OBJECT
public:
  explicit RemoteViewWindow(const QString &client, QWidget *parent = nullptr);

  QString client() const
  {
    return m_client;
  }

  //! The settings to ask the client for (also when asking again after a reconnect)
  QJsonObject request(bool keyFrame) const;

  void handlePiece(const QByteArray &piece);
  void handleInfo(const QJsonObject &info);
  //! The client connected or went away
  void setConnected(bool connected);

Q_SIGNALS:
  //! Ask the core to send \c payload as \c command (kRemoteView, kRemoteAck, kRemoteInput)
  void command(const QString &command, const QJsonObject &payload);
  void closed(const QString &client);

protected:
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void retranslate();
  void updateStatus();
  void askAgain(bool keyFrame);
  void rebuildDisplays(int count);

  QString m_client;
  RemoteViewCanvas *m_canvas = nullptr;
  QLabel *m_dot = nullptr;
  QLabel *m_title = nullptr;
  QLabel *m_status = nullptr;
  mac::SegmentedControl *m_displays = nullptr;
  mac::SegmentedControl *m_quality = nullptr;
  QPushButton *m_fullScreen = nullptr;

  deskflow::remote::FrameAssembler m_assembler;
  deskflow::remote::FrameCanvas m_frames;
  QString m_state = QStringLiteral("connecting");
  QString m_codec;
  QString m_detail;
  int m_display = 0;
  int m_displayCount = 1;
  bool m_connected = true;
  QElapsedTimer m_lastKeyRequest;
  QElapsedTimer m_fpsClock;
  int m_framesThisSecond = 0;
  int m_fps = 0;
  QSize m_frameSize;
};

} // namespace deskflow::gui::remote
