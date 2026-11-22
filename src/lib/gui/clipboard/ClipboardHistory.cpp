/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistory.h"

#include "ClipboardHistoryModel.h"
#include "ClipboardHistoryPopup.h"
#include "ClipboardMonitor.h"
#include "GlobalHotkey.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace deskflow::gui::clipboard {

ClipboardHistory::ClipboardHistory(QWidget *dialogParent, QObject *parent)
    : QObject(parent),
      m_dialogParent(dialogParent),
      m_model(new ClipboardHistoryModel(storageDir(), this)),
      m_monitor(new ClipboardMonitor(m_model, this)),
      m_popup(new ClipboardHistoryPopup(m_model)),
      m_hotkey(new GlobalHotkey(this))
{
  m_showAction = new QAction(tr("Clipboard History"), this);
  m_showAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
  m_showAction->setMenuRole(QAction::NoRole);
  connect(m_showAction, &QAction::triggered, this, &ClipboardHistory::showPopup);

  buildMenu();
  m_popup->setSettingsMenu(m_menu);

  connect(m_hotkey, &GlobalHotkey::activated, this, &ClipboardHistory::togglePopup);
  connect(m_popup, &ClipboardHistoryPopup::copyRequested, this, &ClipboardHistory::copy);
  connect(m_popup, &ClipboardHistoryPopup::pinToggled, this, [this](const QString &id) {
    if (const auto *item = m_model->item(id))
      m_model->setPinned(id, !item->pinned);
  });
  connect(m_popup, &ClipboardHistoryPopup::removeRequested, m_model, &ClipboardHistoryModel::remove);
  connect(m_popup, &ClipboardHistoryPopup::sortModeChanged, this, [](ClipboardHistoryFilterModel::SortMode mode) {
    Settings::setValue(Settings::ClipboardHistory::SortMode, int(mode));
  });
  connect(m_monitor, &ClipboardMonitor::pausedChanged, this, &ClipboardHistory::updateStateUi);
  connect(m_monitor, &ClipboardMonitor::ignoreNextChanged, this, &ClipboardHistory::updateStateUi);
  connect(m_model, &QAbstractItemModel::rowsInserted, this, &ClipboardHistory::stateChanged);
  connect(m_model, &QAbstractItemModel::rowsRemoved, this, &ClipboardHistory::stateChanged);
  connect(m_model, &QAbstractItemModel::modelReset, this, &ClipboardHistory::stateChanged);

  reloadSettings();
}

ClipboardHistory::~ClipboardHistory()
{
  m_model->saveNow();
  delete m_popup;
}

QString ClipboardHistory::storageDir()
{
  return QStringLiteral("%1/clipboard-history").arg(Settings::settingsPath());
}

void ClipboardHistory::handleCoreLogLine(const QString &line)
{
  m_monitor->handleCoreLogLine(line);
}

void ClipboardHistory::showPopup()
{
  if (!Settings::value(Settings::ClipboardHistory::Enabled).toBool())
    return;
  m_popup->present();
}

void ClipboardHistory::togglePopup()
{
  if (!Settings::value(Settings::ClipboardHistory::Enabled).toBool())
    return;
  m_popup->toggle();
}

void ClipboardHistory::reloadSettings()
{
  const bool enabled = Settings::value(Settings::ClipboardHistory::Enabled).toBool();
  m_monitor->setEnabled(enabled);
  m_showAction->setVisible(enabled);

  m_monitor->setPaused(Settings::value(Settings::ClipboardHistory::Paused).toBool());
  m_monitor->setIgnoreSensitive(Settings::value(Settings::ClipboardHistory::IgnoreSensitive).toBool());
  m_monitor->setLocalComputerName(Settings::value(Settings::Core::ComputerName).toString());
  m_monitor->setServerName(Settings::value(Settings::Client::RemoteHost).toString());

  const int maxItems = Settings::value(Settings::ClipboardHistory::MaxItems).toInt();
  m_model->setMaxItems(maxItems > 0 ? maxItems : 200);
  for (auto *action : m_sizeGroup->actions())
    action->setChecked(action->data().toInt() == m_model->maxItems());

  m_popup->setSortMode(
      ClipboardHistoryFilterModel::SortMode(qBound(0, Settings::value(Settings::ClipboardHistory::SortMode).toInt(), 2))
  );

  if (enabled)
    applyShortcut();
  else
    m_hotkey->setShortcut({});

  updateStateUi();
}

void ClipboardHistory::buildMenu()
{
  m_menu = new QMenu(m_dialogParent);

  m_pauseAction = m_menu->addAction(tr("Pause recording"));
  m_pauseAction->setCheckable(true);
  connect(m_pauseAction, &QAction::toggled, this, [this](bool paused) {
    Settings::setValue(Settings::ClipboardHistory::Paused, paused);
    m_monitor->setPaused(paused);
  });

  m_ignoreNextAction = m_menu->addAction(tr("Ignore next copy"));
  m_ignoreNextAction->setToolTip(tr("Handy before copying a password or other one-off secret"));
  connect(m_ignoreNextAction, &QAction::triggered, m_monitor, &ClipboardMonitor::ignoreNextCopy);

  m_sensitiveAction = m_menu->addAction(tr("Skip passwords and concealed content"));
  m_sensitiveAction->setCheckable(true);
  connect(m_sensitiveAction, &QAction::toggled, this, [this](bool ignore) {
    Settings::setValue(Settings::ClipboardHistory::IgnoreSensitive, ignore);
    m_monitor->setIgnoreSensitive(ignore);
  });

  m_menu->addSeparator();

  auto *sizeMenu = m_menu->addMenu(tr("Keep history of"));
  m_sizeGroup = new QActionGroup(sizeMenu);
  for (const int size : {50, 100, 200, 500, 1000, 5000}) {
    auto *action = sizeMenu->addAction(tr("%1 items").arg(size));
    action->setCheckable(true);
    action->setData(size);
    m_sizeGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, size] {
      Settings::setValue(Settings::ClipboardHistory::MaxItems, size);
      m_model->setMaxItems(size);
    });
  }

  m_shortcutAction = m_menu->addAction(tr("Change shortcut…"));
  connect(m_shortcutAction, &QAction::triggered, this, &ClipboardHistory::editShortcut);

  m_menu->addSeparator();
  m_menu->addAction(tr("Clear history (keep pinned)"), this, [this] { clearHistory(false); });
  m_menu->addAction(tr("Clear everything…"), this, [this] { clearHistory(true); });
}

void ClipboardHistory::applyShortcut()
{
  const auto portable = Settings::value(Settings::ClipboardHistory::Shortcut).toString();
  const QKeySequence sequence(portable, QKeySequence::PortableText);
  m_showAction->setShortcut(sequence);
  // The menu action only shows the shortcut; the global hotkey does the work.
  m_showAction->setShortcutContext(Qt::WidgetShortcut);
  m_hotkey->setShortcut(sequence);
  updateStateUi();
}

void ClipboardHistory::editShortcut()
{
  QDialog dialog(m_dialogParent);
  dialog.setWindowTitle(tr("Clipboard History Shortcut"));
  auto *layout = new QVBoxLayout(&dialog);

  auto *label = new QLabel(&dialog);
  label->setWordWrap(true);
  if (GlobalHotkey::isSupported()) {
    label->setText(tr("Press the keys you want to use to open clipboard history from anywhere."));
  } else {
    label->setText(tr("Your desktop does not let apps register global shortcuts (for example on Wayland). "
                      "Add a custom shortcut in your system keyboard settings that runs:\n\n    %1 --%2")
                       .arg(kAppId, kShowCommand));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  layout->addWidget(label);

  auto *edit = new QKeySequenceEdit(
      QKeySequence(Settings::value(Settings::ClipboardHistory::Shortcut).toString(), QKeySequence::PortableText),
      &dialog
  );
  edit->setEnabled(GlobalHotkey::isSupported());
  // Only single chord shortcuts make sense globally.
  connect(edit, &QKeySequenceEdit::keySequenceChanged, edit, [edit](const QKeySequence &sequence) {
    if (sequence.count() > 1)
      edit->setKeySequence(QKeySequence(sequence[0]));
  });
  layout->addWidget(edit);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto *reset = buttons->addButton(tr("Use default"), QDialogButtonBox::ResetRole);
  connect(reset, &QPushButton::clicked, edit, [edit] {
    edit->setKeySequence(QKeySequence(
        Settings::defaultValue(Settings::ClipboardHistory::Shortcut).toString(), QKeySequence::PortableText
    ));
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return;

  const auto previous = Settings::value(Settings::ClipboardHistory::Shortcut).toString();
  const auto chosen = edit->keySequence().toString(QKeySequence::PortableText);
  Settings::setValue(Settings::ClipboardHistory::Shortcut, chosen);
  applyShortcut();

  if (!chosen.isEmpty() && GlobalHotkey::isSupported() && !m_hotkey->isRegistered()) {
    QMessageBox::warning(
        m_dialogParent, tr("Shortcut unavailable"),
        tr("%1 is already used by another application. Your previous shortcut has been restored.")
            .arg(edit->keySequence().toString(QKeySequence::NativeText))
    );
    Settings::setValue(Settings::ClipboardHistory::Shortcut, previous);
    applyShortcut();
  }
}

void ClipboardHistory::clearHistory(bool includePinned)
{
  if (includePinned) {
    const auto answer = QMessageBox::question(
        m_dialogParent, tr("Clear clipboard history"),
        tr("Delete every item in your clipboard history, including pinned items? This cannot be undone.")
    );
    if (answer != QMessageBox::Yes)
      return;
  }
  m_model->clear(!includePinned);
  m_model->saveNow();
  Q_EMIT stateChanged();
}

void ClipboardHistory::updateStateUi()
{
  {
    const QSignalBlocker pauseBlocker(m_pauseAction);
    m_pauseAction->setChecked(m_monitor->isPaused());
  }
  {
    const QSignalBlocker sensitiveBlocker(m_sensitiveAction);
    m_sensitiveAction->setChecked(Settings::value(Settings::ClipboardHistory::IgnoreSensitive).toBool());
  }

  const auto shortcut = m_hotkey->shortcut().toString(QKeySequence::NativeText);
  m_shortcutAction->setText(
      shortcut.isEmpty()
          ? tr("Set shortcut…")
          : tr("Change shortcut (%1)%2…").arg(shortcut, m_hotkey->isRegistered() ? QString() : tr(", unavailable"))
  );

  m_popup->setRecordingState(m_monitor->isPaused(), m_monitor->isIgnoringNextCopy());
  Q_EMIT stateChanged();
}

QString ClipboardHistory::shortcutText(bool *registered) const
{
  if (registered)
    *registered = m_hotkey->isRegistered();
  return m_hotkey->shortcut().toString(QKeySequence::NativeText);
}

int ClipboardHistory::itemCount() const
{
  return m_model->rowCount();
}

void ClipboardHistory::copy(const QString &id, bool plainTextOnly)
{
  m_monitor->copyToClipboard(id, plainTextOnly);
}

} // namespace deskflow::gui::clipboard
