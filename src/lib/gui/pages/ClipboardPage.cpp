/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardPage.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/clipboard/ClipboardHistory.h"
#include "gui/clipboard/GlobalHotkey.h"
#include "gui/widgets/MacControls.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;
using deskflow::gui::clipboard::ClipboardHistory;
using deskflow::gui::clipboard::GlobalHotkey;

ClipboardPage::ClipboardPage(ClipboardHistory *history, QWidget *parent) : RebuildablePage(parent), m_history{history}
{
  connect(m_history, &ClipboardHistory::stateChanged, this, &ClipboardPage::updateState);
}

void ClipboardPage::build(Page *page)
{
  const bool enabled = Settings::value(Settings::ClipboardHistory::Enabled).toBool();

  page->setTitle(tr("Clipboard History"));
  page->setSubtitle(
      tr("Everything you copy on any linked computer is kept here, on this computer only. "
         "Pick an item to paste it anywhere.")
  );

  auto *open = new QPushButton(tr("Open Clipboard History"));
  open->setProperty("primary", true);
  open->setEnabled(enabled);
  connect(open, &QPushButton::clicked, m_history, &ClipboardHistory::showPopup);
  page->setTrailing(open);

  // recording
  page->addSection(tr("Recording"));
  auto *recording = new FormCard();

  auto *enable = new ToggleSwitch();
  enable->setChecked(enabled);
  connect(enable, &ToggleSwitch::toggled, this, [this](bool on) {
    Settings::setValue(Settings::ClipboardHistory::Enabled, on);
    m_history->reloadSettings();
    scheduleRebuild();
  });
  recording->addRow(tr("Keep clipboard history"), QString(), enable);

  auto *pause = new ToggleSwitch();
  pause->setChecked(Settings::value(Settings::ClipboardHistory::Paused).toBool());
  pause->setEnabled(enabled);
  connect(pause, &ToggleSwitch::toggled, this, [this](bool on) {
    Settings::setValue(Settings::ClipboardHistory::Paused, on);
    m_history->reloadSettings();
  });
  recording->addRow(tr("Pause recording"), tr("New copies are not saved until you turn this off."), pause);

  auto *sensitive = new ToggleSwitch();
  sensitive->setChecked(Settings::value(Settings::ClipboardHistory::IgnoreSensitive).toBool());
  sensitive->setEnabled(enabled);
  connect(sensitive, &ToggleSwitch::toggled, this, [this](bool on) {
    Settings::setValue(Settings::ClipboardHistory::IgnoreSensitive, on);
    m_history->reloadSettings();
  });
  recording->addRow(
      tr("Skip passwords"), tr("Password managers mark what they copy as concealed; those items are never saved."),
      sensitive
  );

  auto *size = new QComboBox();
  const int maxItems = Settings::value(Settings::ClipboardHistory::MaxItems).toInt();
  for (const int n : {50, 100, 200, 500, 1000, 5000}) {
    size->addItem(tr("%1 items").arg(n), n);
    if (n == maxItems)
      size->setCurrentIndex(size->count() - 1);
  }
  size->setEnabled(enabled);
  connect(size, &QComboBox::currentIndexChanged, this, [this, size] {
    Settings::setValue(Settings::ClipboardHistory::MaxItems, size->currentData());
    m_history->reloadSettings();
  });
  const auto sizeRow = recording->addRow(tr("Keep up to"), tr("The oldest unpinned items go first."), size);
  m_count = sizeRow.detail;
  page->addWidget(recording);

  // shortcut
  page->addSection(tr("Shortcut"));
  auto *shortcutCard = new FormCard();
  auto *shortcutControls = new QWidget();
  auto *shortcutLayout = new QHBoxLayout(shortcutControls);
  shortcutLayout->setContentsMargins(0, 0, 0, 0);
  shortcutLayout->setSpacing(10);
  m_shortcut = label(QString(), "keycap");
  shortcutLayout->addWidget(m_shortcut);
  auto *change = new QPushButton(tr("Change…"));
  change->setEnabled(enabled);
  connect(change, &QPushButton::clicked, m_history, &ClipboardHistory::editShortcut);
  shortcutLayout->addWidget(change);
  const auto detail = GlobalHotkey::isSupported()
                          ? tr("Works from any app, on whichever computer the cursor is on.")
                          : tr("This desktop doesn't let apps register shortcuts. Add one in your system settings "
                               "that runs: %1 --%2")
                                .arg(kAppId, ClipboardHistory::kShowCommand);
  shortcutCard->addRow(tr("Open clipboard history"), detail, shortcutControls);
  page->addWidget(shortcutCard);

  // clearing
  page->addSection(tr("Clear"));
  auto *clearCard = new FormCard();
  auto *clear = new QPushButton(tr("Clear"));
  connect(clear, &QPushButton::clicked, this, [this] { m_history->clearHistory(false); });
  clearCard->addRow(tr("Clear history"), tr("Pinned items stay."), clear);
  auto *clearAll = new QPushButton(tr("Clear Everything…"));
  clearAll->setProperty("destructive", true);
  connect(clearAll, &QPushButton::clicked, this, [this] { m_history->clearHistory(true); });
  clearCard->addRow(tr("Clear everything"), tr("Removes pinned items too. This can't be undone."), clearAll);
  page->addWidget(clearCard);

  updateState();
}

void ClipboardPage::updateState()
{
  if (m_shortcut) {
    bool registered = true;
    const auto text = m_history->shortcutText(&registered);
    m_shortcut->setText(text.isEmpty() ? tr("None") : text);
    m_shortcut->setToolTip(
        registered || text.isEmpty() ? QString() : tr("Another app is using this shortcut; choose a different one.")
    );
    m_shortcut->setProperty("error", !registered && !text.isEmpty());
  }
  if (m_count)
    m_count->setText(tr("The oldest unpinned items go first. %n saved now.", nullptr, m_history->itemCount()));
}

} // namespace deskflow::gui::pages
