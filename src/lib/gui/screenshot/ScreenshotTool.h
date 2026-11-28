/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>

class QAction;
class QProcess;
class QWidget;

namespace deskflow::gui::clipboard {
class GlobalHotkey;
}

namespace deskflow::gui::screenshot {

class ScreenshotEditor;
class ScreenshotThumbnail;

/**
 * @brief The screenshot tool: a global shortcut, the capture, the thumbnail and the editor.
 *
 * A capture always lands on the clipboard (and so in clipboard history and on
 * every linked computer). A thumbnail then slides in at the bottom right;
 * clicking it, or choosing Edit on the image in clipboard history, opens the
 * editor to crop and mark it up.
 */
class ScreenshotTool : public QObject
{
  Q_OBJECT
public:
  //! Command line flag and IPC message that take a screenshot in a running instance.
  inline static const auto kCommand = QStringLiteral("screenshot");

  explicit ScreenshotTool(QWidget *dialogParent, QObject *parent = nullptr);
  ~ScreenshotTool() override;

  //! "Take Screenshot" action for menus; shows the configured shortcut.
  QAction *captureAction() const
  {
    return m_action;
  }

  void capture();
  //! Open the editor on @p image (e.g. from clipboard history).
  void edit(const QImage &image);

  void reloadSettings();
  void editShortcut();
  //! The shortcut as shown to the user, empty when none; @p registered is false when another app holds it.
  QString shortcutText(bool *registered = nullptr) const;

  ScreenshotThumbnail *thumbnail() const
  {
    return m_thumbnail;
  }

  //! Save @p image as a new file in the configured folder; returns its path, empty on failure.
  static QString saveToFolder(const QImage &image);

Q_SIGNALS:
  void stateChanged();

private:
  void applyShortcut();
  void captured(const QImage &image);
  void captureWithSelector();
  void copyToClipboard(const QImage &image);

  QWidget *m_dialogParent;
  clipboard::GlobalHotkey *m_hotkey;
  QAction *m_action;
  ScreenshotThumbnail *m_thumbnail;
  QProcess *m_process = nullptr;
  bool m_busy = false;
  QList<QPointer<ScreenshotEditor>> m_editors;
};

} // namespace deskflow::gui::screenshot
