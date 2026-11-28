/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QObject>

class QAction;
class QActionGroup;
class QMenu;
class QWidget;

namespace deskflow::gui::clipboard {

class ClipboardHistoryModel;
class ClipboardHistoryPopup;
class ClipboardMonitor;
class GlobalHotkey;

/**
 * @brief Clipboard history feature: recording, the popup panel and its global shortcut.
 *
 * Every computer running the Deskflow GUI keeps its own history. Because the
 * clipboard is shared, anything copied on any linked computer lands in the
 * history, tagged with the computer it came from, and picking an item puts it
 * back on the shared clipboard.
 */
class ClipboardHistory : public QObject
{
  Q_OBJECT
public:
  /// Command line flag and IPC message that open the panel in a running instance.
  inline static const auto kShowCommand = QStringLiteral("clipboard-history");

  explicit ClipboardHistory(QWidget *dialogParent, QObject *parent = nullptr);
  ~ClipboardHistory() override;

  /// "Clipboard History" action for menus; shows the configured shortcut.
  QAction *showAction() const
  {
    return m_showAction;
  }

  void handleCoreLogLine(const QString &line);
  void showPopup();
  void togglePopup();

  /// Re-read settings, e.g. after the computer name or server address changed.
  void reloadSettings();

  static QString storageDir();

  //! Ask for a new global shortcut.
  void editShortcut();
  //! Remove items; asks first when pinned items go too.
  void clearHistory(bool includePinned);
  //! The shortcut as shown to the user, empty when none; @p registered is false when another app holds it.
  QString shortcutText(bool *registered = nullptr) const;
  //! Number of items kept.
  int itemCount() const;

Q_SIGNALS:
  //! The shortcut, pause state or item count changed.
  void stateChanged();
  //! The user chose to edit an image item.
  void editImageRequested(const QImage &image);

private:
  void buildMenu();
  void applyShortcut();
  void updateStateUi();
  void copy(const QString &id, bool plainTextOnly);

  QWidget *m_dialogParent;
  ClipboardHistoryModel *m_model;
  ClipboardMonitor *m_monitor;
  ClipboardHistoryPopup *m_popup;
  GlobalHotkey *m_hotkey;

  QAction *m_showAction = nullptr;
  QMenu *m_menu = nullptr;
  QAction *m_pauseAction = nullptr;
  QAction *m_ignoreNextAction = nullptr;
  QAction *m_sensitiveAction = nullptr;
  QAction *m_shortcutAction = nullptr;
  QActionGroup *m_sizeGroup = nullptr;
};

} // namespace deskflow::gui::clipboard
