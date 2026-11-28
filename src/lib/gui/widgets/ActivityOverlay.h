/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QColor>
#include <QList>
#include <QSet>
#include <QWidget>

class QTimer;

/**
 * @brief Translucent always-on-top overlay showing key presses and deskflow activity.
 *
 * Similar to screencast key viewers: dark rounded bubbles in the top-right corner of the
 * screen that fade out after a moment. Each key press is its own small bubble in one row,
 * activity events stack underneath. Mouse input passes through and it never takes focus.
 * Nothing shown here is written to the log.
 */
class ActivityOverlay : public QWidget
{
  Q_OBJECT

public:
  enum class Kind
  {
    Keys,
    Switch,
    Clipboard,
    Connected,
    Disconnected,
    Info
  };
  Q_ENUM(Kind)

  explicit ActivityOverlay(QWidget *parent = nullptr);

  void setShowKeys(bool show);
  void setShowActivity(bool show);
  bool showKeys() const
  {
    return m_showKeys;
  }
  bool showActivity() const
  {
    return m_showActivity;
  }

  /**
   * @brief Show a key press.
   * @param label Text from the core, e.g. `a`, `Ctrl+C`, `↵`.
   * @param isText Plain typing. Never merged; only repeated shortcuts collapse into `×N`.
   */
  void addKey(const QString &label, bool isText);

  //! Show an activity event with a coloured marker.
  void addActivity(Kind kind, const QString &text, bool always = false);

  /**
   * @brief Turn an `activity` message from the core into a bubble.
   * @param type First field of the message, e.g. `switched`.
   * @param fields Remaining (already decoded) fields.
   */
  void handleCoreActivity(const QString &type, const QStringList &fields);

  //! Server mode: diff the list of connected clients into connect/disconnect bubbles.
  void handleServerClients(const QStringList &clients);

  //! Client mode: connect/disconnect bubbles from the core connection state.
  void handleClientConnectionState(deskflow::core::ConnectionState state);

  //! Forget the client list and connection state (core stopped or mode changed).
  void resetConnectionTracking();

  //! Remove all bubbles.
  void clear();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  struct Entry
  {
    Kind kind = Kind::Info;
    QString text;
    qint64 created = 0;
    qint64 lastUpdate = 0;
    qint64 lifetime = 0;
    int repeat = 1;
    bool isText = false;
  };

  void push(Entry entry);
  void tick();
  void reposition();
  qint64 now() const;
  static qreal opacityFor(const Entry &entry, qint64 t);
  static QColor accentFor(Kind kind);
  static QString glyphFor(Kind kind);

  QList<Entry> m_entries;
  QTimer *m_timer;
  bool m_showKeys = false;
  bool m_showActivity = false;
  QSet<QString> m_knownClients;
  bool m_clientsKnown = false;
  deskflow::core::ConnectionState m_clientState = deskflow::core::ConnectionState::Disconnected;
};
