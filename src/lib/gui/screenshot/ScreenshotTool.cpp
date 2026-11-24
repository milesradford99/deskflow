/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotTool.h"

#include "RegionSelector.h"
#include "ScreenshotEditor.h"
#include "ScreenshotThumbnail.h"

#include "common/Settings.h"
#include "gui/clipboard/GlobalHotkey.h"
#include "gui/clipboard/ShortcutDialog.h"

#if defined(Q_OS_MACOS)
#include "gui/OSXHelpers.h"
#endif

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWidget>

namespace deskflow::gui::screenshot {

using clipboard::GlobalHotkey;

ScreenshotTool::ScreenshotTool(QWidget *dialogParent, QObject *parent)
    : QObject(parent),
      m_dialogParent(dialogParent),
      m_hotkey(new GlobalHotkey(this)),
      m_action(new QAction(tr("Take Screenshot"), this)),
      m_thumbnail(new ScreenshotThumbnail())
{
  m_action->setMenuRole(QAction::NoRole);
  connect(m_action, &QAction::triggered, this, [this] {
    // Let the menu close first so it isn't in the picture.
    QTimer::singleShot(250, this, &ScreenshotTool::capture);
  });
  connect(m_hotkey, &GlobalHotkey::activated, this, &ScreenshotTool::capture);
  connect(m_thumbnail, &ScreenshotThumbnail::editRequested, this, &ScreenshotTool::edit);
  connect(m_thumbnail, &ScreenshotThumbnail::saveRequested, this, [](const QImage &image) { saveToFolder(image); });
  reloadSettings();
}

ScreenshotTool::~ScreenshotTool()
{
  delete m_thumbnail;
  for (const auto &editor : std::as_const(m_editors)) {
    if (editor)
      editor->deleteLater();
  }
}

void ScreenshotTool::reloadSettings()
{
  const bool enabled = Settings::value(Settings::Screenshot::Enabled).toBool();
  m_action->setVisible(enabled);
  if (enabled)
    applyShortcut();
  else
    m_hotkey->setShortcut({});
  Q_EMIT stateChanged();
}

void ScreenshotTool::applyShortcut()
{
  const QKeySequence sequence(Settings::value(Settings::Screenshot::Shortcut).toString(), QKeySequence::PortableText);
  m_action->setShortcut(sequence);
  // The menu only shows the shortcut; the global hotkey does the work.
  m_action->setShortcutContext(Qt::WidgetShortcut);
  m_hotkey->setShortcut(sequence);
  Q_EMIT stateChanged();
}

void ScreenshotTool::editShortcut()
{
  const auto chosen = clipboard::askForGlobalShortcut(
      m_dialogParent, tr("Screenshot Shortcut"),
      tr("Press the keys you want to use to take a screenshot from anywhere."), kCommand,
      QKeySequence(Settings::value(Settings::Screenshot::Shortcut).toString(), QKeySequence::PortableText),
      QKeySequence(Settings::defaultValue(Settings::Screenshot::Shortcut).toString(), QKeySequence::PortableText)
  );
  if (!chosen)
    return;

  const auto previous = Settings::value(Settings::Screenshot::Shortcut).toString();
  Settings::setValue(Settings::Screenshot::Shortcut, chosen->toString(QKeySequence::PortableText));
  applyShortcut();

  if (!chosen->isEmpty() && GlobalHotkey::isSupported() && !m_hotkey->isRegistered()) {
    QMessageBox::warning(
        m_dialogParent, tr("Shortcut unavailable"),
        tr("%1 is already used by another application. Your previous shortcut has been restored.")
            .arg(chosen->toString(QKeySequence::NativeText))
    );
    Settings::setValue(Settings::Screenshot::Shortcut, previous);
    applyShortcut();
  }
}

QString ScreenshotTool::shortcutText(bool *registered) const
{
  if (registered)
    *registered = m_hotkey->isRegistered();
  return m_hotkey->shortcut().toString(QKeySequence::NativeText);
}

void ScreenshotTool::capture()
{
  if (!Settings::value(Settings::Screenshot::Enabled).toBool() || m_busy)
    return;
  m_busy = true;
  // Like macOS: the previous thumbnail must not end up in the next picture.
  m_thumbnail->hideNow();

#if defined(Q_OS_MACOS)
  // The system tool gives the native crosshair, Space for windows and Retina pixels.
  // Without screen recording access it only sees the desktop, so ask once.
  if (!macScreenCaptureAllowed())
    macRequestScreenCaptureAccess();

  const auto dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const auto path =
      QDir(dir).filePath(QStringLiteral("deskflow-shot-%1.png").arg(QUuid::createUuid().toString(QUuid::Id128)));
  m_process = new QProcess(this);
  connect(m_process, &QProcess::finished, this, [this, path] {
    m_process->deleteLater();
    m_process = nullptr;
    QImage image(path);
    QFile::remove(path);
    m_busy = false;
    if (image.isNull())
      return; // cancelled
    if (const auto *screen = QGuiApplication::screenAt(QCursor::pos()))
      image.setDevicePixelRatio(screen->devicePixelRatio());
    captured(image);
  });
  connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    m_process->deleteLater();
    m_process = nullptr;
    m_busy = false;
    captureWithSelector();
  });
  m_process->start(QStringLiteral("/usr/sbin/screencapture"), {QStringLiteral("-i"), path});
#else
  m_busy = false;
  captureWithSelector();
#endif
}

void ScreenshotTool::captureWithSelector()
{
  m_busy = true;
  auto *selector = new RegionSelector();
  connect(selector, &RegionSelector::captured, this, [this](const QImage &image) {
    m_busy = false;
    captured(image);
  });
  connect(selector, &RegionSelector::cancelled, this, [this] { m_busy = false; });
  if (!selector->start()) {
    m_busy = false;
    selector->deleteLater();
    QMessageBox::information(
        m_dialogParent, tr("Screenshot"),
        tr("This desktop doesn't let apps read the screen (for example on Wayland). "
           "Use your desktop's own screenshot tool; whatever it copies still shows up in clipboard history.")
    );
  }
}

void ScreenshotTool::captured(const QImage &image)
{
  copyToClipboard(image);
  if (Settings::value(Settings::Screenshot::SaveToFolder).toBool())
    saveToFolder(image);
  if (Settings::value(Settings::Screenshot::ShowThumbnail).toBool()) {
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    m_thumbnail->present(image, screen, Settings::value(Settings::Screenshot::ThumbnailSeconds).toInt());
  }
}

void ScreenshotTool::copyToClipboard(const QImage &image)
{
  QImage plain = image;
  plain.setDevicePixelRatio(1);
  QGuiApplication::clipboard()->setImage(plain);
}

void ScreenshotTool::edit(const QImage &image)
{
  if (image.isNull())
    return;
  QImage source = image;
  if (source.devicePixelRatio() <= 1) {
    // Images from clipboard history carry no ratio; assume the screen they will be edited on.
    if (const auto *screen = QGuiApplication::screenAt(QCursor::pos()))
      source.setDevicePixelRatio(screen->devicePixelRatio());
  }
  auto *editor = new ScreenshotEditor(source);
  connect(editor, &ScreenshotEditor::copyRequested, this, &ScreenshotTool::copyToClipboard);
  m_editors.removeAll(nullptr);
  m_editors.append(editor);
#if defined(Q_OS_MACOS)
  forceAppActive();
#endif
  editor->show();
  editor->raise();
  editor->activateWindow();
}

QString ScreenshotTool::saveToFolder(const QImage &image)
{
  auto folder = Settings::value(Settings::Screenshot::Folder).toString();
  if (folder.isEmpty())
    folder = Settings::defaultValue(Settings::Screenshot::Folder).toString();
  QDir().mkpath(folder);
  const auto path = QDir(folder).filePath(ScreenshotEditor::defaultFileName());
  QImage plain = image;
  plain.setDevicePixelRatio(1);
  if (!plain.save(path)) {
    qWarning().noquote() << "unable to save screenshot to" << path;
    return {};
  }
  qInfo().noquote() << "screenshot saved to" << path;
  return path;
}

} // namespace deskflow::gui::screenshot
