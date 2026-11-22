/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "ClipboardHistoryItem.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <optional>

class QClipboard;
class QMimeData;
class QTimer;

namespace deskflow::gui::clipboard {

class ClipboardHistoryModel;

/**
 * @brief Watches the system clipboard and records changes in the history.
 *
 * When Deskflow syncs a clipboard from another computer the core logs which
 * computer it came from; feeding those log lines to handleCoreLogLine() lets
 * the history show where each item was copied.
 */
class ClipboardMonitor : public QObject
{
  Q_OBJECT
public:
  explicit ClipboardMonitor(ClipboardHistoryModel *model, QObject *parent = nullptr);

  void setEnabled(bool enabled);
  bool isEnabled() const
  {
    return m_enabled;
  }

  void setPaused(bool paused);
  bool isPaused() const
  {
    return m_paused;
  }

  /// Do not record the next clipboard change, for example a one-off password.
  void ignoreNextCopy();
  bool isIgnoringNextCopy() const
  {
    return m_ignoreNext;
  }

  void setIgnoreSensitive(bool ignore)
  {
    m_ignoreSensitive = ignore;
  }

  void setLocalComputerName(const QString &name)
  {
    m_localName = name;
  }

  /// Name used for content synced from the server when running as a client.
  void setServerName(const QString &name)
  {
    m_serverName = name;
  }

  /// Parse a line of core output looking for clipboard sync messages.
  void handleCoreLogLine(const QString &line);

  /// Put a history item back on the clipboard (and so on every linked computer).
  bool copyToClipboard(const QString &id, bool plainTextOnly = false);

  /// Read the clipboard now; normally driven by change notifications.
  void readClipboard();

  // Helpers exposed for unit tests.
  static bool isSensitive(const QMimeData *mime);
  static ItemKind detectTextKind(const QString &text);
  static std::optional<HistoryItem> itemFromMimeData(const QMimeData *mime, QImage *image);
  /**
   * @return std::nullopt when the line is not about the clipboard, an empty string when the content
   *         came from this computer, kFromServer when a client received the server's clipboard,
   *         otherwise the name of the computer it came from
   */
  static std::optional<QString> remoteSourceFromLogLine(const QString &line, const QString &localName);
  inline static const QString kFromServer = QStringLiteral("\x01server");

  static constexpr qsizetype kMaxTextLength = 1024 * 1024;
  static constexpr qsizetype kMaxHtmlLength = 512 * 1024;
  static constexpr qint64 kMaxImagePixels = 40'000'000;

Q_SIGNALS:
  void pausedChanged(bool paused);
  void ignoreNextChanged(bool ignoring);

private:
  void clipboardChanged();
  void attributeSource(const QString &source);
  void pollPasteboard();

  ClipboardHistoryModel *m_model;
  QClipboard *m_clipboard;
  QTimer *m_debounce;
  QTimer *m_poll = nullptr;
  long m_lastChangeCount = -1;

  bool m_enabled = true;
  bool m_paused = false;
  bool m_ignoreNext = false;
  bool m_ignoreSensitive = true;
  QByteArray m_selfSetHash;
  QString m_localName;
  QString m_serverName;

  // Source attribution: the core log line and the clipboard change can arrive in either order.
  QString m_pendingSource;
  QElapsedTimer m_pendingSourceTimer;
  QString m_lastAddedId;
  QElapsedTimer m_lastAddedTimer;
};

} // namespace deskflow::gui::clipboard
