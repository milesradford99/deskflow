/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>

namespace deskflow::gui::clipboard {

/**
 * @brief A system wide keyboard shortcut for the GUI process.
 *
 * Uses RegisterHotKey on Windows, Carbon hot keys on macOS and a passive key
 * grab on X11. Wayland compositors do not allow this, so isSupported() is
 * false there and the shortcut must be bound in the desktop settings instead.
 */
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
  Q_OBJECT
public:
  explicit GlobalHotkey(QObject *parent = nullptr);
  ~GlobalHotkey() override;

  static bool isSupported();

  /// Register @p sequence (only its first chord is used). An empty sequence unregisters.
  bool setShortcut(const QKeySequence &sequence);
  QKeySequence shortcut() const
  {
    return m_sequence;
  }
  bool isRegistered() const
  {
    return m_registered;
  }
  /// Distinguishes this shortcut from the app's other global shortcuts.
  quint32 id() const
  {
    return m_id;
  }

  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

  /// Called by the platform callback.
  void trigger();

Q_SIGNALS:
  void activated();

private:
  bool registerNative(Qt::Key key, Qt::KeyboardModifiers modifiers);
  void unregisterNative();

  QKeySequence m_sequence;
  bool m_registered = false;
  quint32 m_id = 0;
  bool m_filterInstalled = false;

  // Platform specific state.
  quint32 m_nativeKey = 0;
  quint32 m_nativeMods = 0;
  void *m_platformRef = nullptr;
  void *m_platformHandler = nullptr;
};

} // namespace deskflow::gui::clipboard
