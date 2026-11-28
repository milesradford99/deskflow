/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotPage.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/clipboard/GlobalHotkey.h"
#include "gui/screenshot/ScreenshotTool.h"
#include "gui/widgets/MacControls.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;
using deskflow::gui::clipboard::GlobalHotkey;
using deskflow::gui::screenshot::ScreenshotTool;

ScreenshotPage::ScreenshotPage(ScreenshotTool *tool, QWidget *parent) : RebuildablePage(parent), m_tool{tool}
{
  connect(m_tool, &ScreenshotTool::stateChanged, this, &ScreenshotPage::updateState);
}

void ScreenshotPage::build(Page *page)
{
  const bool enabled = Settings::value(Settings::Screenshot::Enabled).toBool();

  page->setTitle(tr("Screenshot"));
  page->setSubtitle(
      tr("Capture part of the screen straight to the clipboard, ready to paste on any linked computer. "
         "Click the thumbnail that appears to crop it or mark it up.")
  );

  auto *take = new QPushButton(tr("Take Screenshot"));
  take->setProperty("primary", true);
  take->setEnabled(enabled);
  connect(take, &QPushButton::clicked, m_tool->captureAction(), &QAction::trigger);
  page->setTrailing(take);

  // capture
  page->addSection(tr("Capture"));
  auto *capture = new FormCard();

  auto *enable = new ToggleSwitch();
  enable->setChecked(enabled);
  connect(enable, &ToggleSwitch::toggled, this, [this](bool on) {
    Settings::setValue(Settings::Screenshot::Enabled, on);
    m_tool->reloadSettings();
    scheduleRebuild();
  });
  capture->addRow(tr("Screenshot tool"), QString(), enable);

  auto *shortcutControls = new QWidget();
  auto *shortcutLayout = new QHBoxLayout(shortcutControls);
  shortcutLayout->setContentsMargins(0, 0, 0, 0);
  shortcutLayout->setSpacing(10);
  m_shortcut = label(QString(), "keycap");
  shortcutLayout->addWidget(m_shortcut);
  auto *change = new QPushButton(tr("Change…"));
  change->setEnabled(enabled);
  connect(change, &QPushButton::clicked, m_tool, &ScreenshotTool::editShortcut);
  shortcutLayout->addWidget(change);
#ifdef Q_OS_MACOS
  const auto how = tr("Drag to select an area, or press Space to pick a window. The first time, macOS asks "
                      "to allow screen recording for %1.")
                       .arg(kAppName);
#else
  const auto how = tr("Drag to select an area, or click for the whole screen.");
#endif
  const auto detail = GlobalHotkey::isSupported()
                          ? how
                          : tr("This desktop doesn't let apps register shortcuts. Add one in your system settings "
                               "that runs: %1 --%2")
                                .arg(kAppId, ScreenshotTool::kCommand);
  capture->addRow(tr("Shortcut"), detail, shortcutControls);
  page->addWidget(capture);

  // after a capture
  page->addSection(tr("After a capture"));
  auto *after = new FormCard();
  after->addRow(
      tr("Copy to the clipboard"),
      tr("Always. It also lands in clipboard history, where you can open it in the editor again later.")
  );

  auto *thumbnail = new ToggleSwitch();
  thumbnail->setChecked(Settings::value(Settings::Screenshot::ShowThumbnail).toBool());
  thumbnail->setEnabled(enabled);
  connect(thumbnail, &ToggleSwitch::toggled, this, [this](bool on) {
    Settings::setValue(Settings::Screenshot::ShowThumbnail, on);
    scheduleRebuild();
  });
  after->addRow(
      tr("Show floating thumbnail"),
      tr("Slides in at the bottom right. Click it to crop and mark up, or drag it into another app."), thumbnail
  );

  auto *seconds = new QComboBox();
  const int current = Settings::value(Settings::Screenshot::ThumbnailSeconds).toInt();
  for (const int n : {3, 5, 8, 15}) {
    seconds->addItem(tr("%n seconds", nullptr, n), n);
    if (n == current)
      seconds->setCurrentIndex(seconds->count() - 1);
  }
  seconds->setEnabled(enabled && thumbnail->isChecked());
  connect(seconds, &QComboBox::currentIndexChanged, this, [seconds] {
    Settings::setValue(Settings::Screenshot::ThumbnailSeconds, seconds->currentData());
  });
  after->addRow(tr("Thumbnail stays for"), tr("Longer while the pointer is over it."), seconds);

  auto *save = new ToggleSwitch();
  save->setChecked(Settings::value(Settings::Screenshot::SaveToFolder).toBool());
  save->setEnabled(enabled);
  connect(save, &ToggleSwitch::toggled, this, [](bool on) {
    Settings::setValue(Settings::Screenshot::SaveToFolder, on);
  });
  after->addRow(tr("Also save as a file"), tr("A PNG in the folder below, named by date and time."), save);

  auto *choose = new QPushButton(tr("Choose…"));
  connect(choose, &QPushButton::clicked, this, [this] {
    const auto folder = QFileDialog::getExistingDirectory(
        this, tr("Save Screenshots To"), Settings::value(Settings::Screenshot::Folder).toString()
    );
    if (folder.isEmpty())
      return;
    Settings::setValue(Settings::Screenshot::Folder, folder);
    scheduleRebuild();
  });
  after->addRow(
      tr("Folder"), QDir::toNativeSeparators(Settings::value(Settings::Screenshot::Folder).toString()), choose
  );
  page->addWidget(after);

  // editor
  page->addSection(tr("Editor"));
  auto *editor = new FormCard();
  editor->addRow(
      tr("Tools"), tr("Crop, rectangle, ellipse, arrow, line, pen, highlighter, text, numbered steps and pixelate. "
                      "Press Enter to copy the result, or save it as a file.")
  );
  editor->addRow(
      tr("Edit from clipboard history"),
      tr("Select any image in clipboard history and choose Edit (%1).")
          .arg(QKeySequence(QStringLiteral("Ctrl+E")).toString(QKeySequence::NativeText))
  );
  page->addWidget(editor);

  updateState();
}

void ScreenshotPage::updateState()
{
  if (!m_shortcut)
    return;
  bool registered = true;
  const auto text = m_tool->shortcutText(&registered);
  m_shortcut->setText(text.isEmpty() ? tr("None") : text);
  m_shortcut->setToolTip(
      registered || text.isEmpty() ? QString() : tr("Another app is using this shortcut; choose a different one.")
  );
  m_shortcut->setProperty("error", !registered && !text.isEmpty());
}

} // namespace deskflow::gui::pages
