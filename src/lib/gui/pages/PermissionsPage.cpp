/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PermissionsPage.h"

#include "common/Constants.h"
#include "gui/permissions/Permissions.h"
#include "gui/widgets/MacControls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;
using deskflow::gui::permissions::Kind;
using deskflow::gui::permissions::PermissionMonitor;
using deskflow::gui::permissions::State;

namespace {

QLabel *pill(const QString &text, const char *state)
{
  auto *label = new QLabel(text);
  label->setProperty("status", true);
  label->setProperty("state", state);
  return label;
}

} // namespace

PermissionsPage::PermissionsPage(PermissionMonitor *monitor, QWidget *parent)
    : RebuildablePage(parent),
      m_monitor{monitor}
{
  connect(m_monitor, &PermissionMonitor::changed, this, [this] {
    if (isVisible())
      scheduleRebuild();
  });
}

void PermissionsPage::build(Page *page)
{
  page->setTitle(tr("Permissions"));
  page->setSubtitle(
      tr("macOS asks you once to allow each of these in System Settings. This page checks them live: switch one on "
         "and come back, there is nothing to save.")
  );

  if (!m_monitor->supported()) {
    auto *card = new FormCard();
    card->addRow(tr("Nothing to allow"), tr("This system doesn't ask for per-app permissions."));
    page->addWidget(card);
    return;
  }

  const int attention = m_monitor->attentionCount();
  if (m_monitor->state(Kind::Accessibility) == State::Granted && attention == 0)
    page->setTrailing(pill(tr("All set"), "ok"));

  bool anyStale = false;
  for (const auto kind : permissions::kAllKinds)
    anyStale = anyStale || m_monitor->state(kind) == State::Stale;
  if (anyStale) {
    auto *banner = new QFrame();
    banner->setProperty("banner", true);
    auto *bannerLayout = new QVBoxLayout(banner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    bannerLayout->setSpacing(2);
    bannerLayout->addWidget(label(tr("Deskflow was updated, so macOS stopped honouring some switches"), "heading"));
    auto *text = label(
        tr("System Settings can still show %1 switched on, but that entry belongs to the previous build. Click "
           "Repair: %1 removes its old entry and asks again, then switch it on in the list that opens.")
            .arg(kAppName),
        "secondary"
    );
    text->setWordWrap(true);
    bannerLayout->addWidget(text);
    page->addWidget(banner);
  }

  page->addSection(tr("What %1 needs").arg(kAppName));
  auto *card = new FormCard();
  for (const auto kind : permissions::kAllKinds) {
    QString title;
    QString purpose;
    switch (kind) {
    case Kind::Accessibility:
      title = tr("Accessibility");
      purpose = tr("Required. Lets %1 move the pointer and type on this Mac, and share its keyboard and mouse.")
                    .arg(kAppName);
      break;
    case Kind::InputMonitoring:
      title = tr("Input Monitoring");
      purpose =
          tr("Recommended when this Mac shares its keyboard. Some macOS versions want it as well as "
             "Accessibility before keys reach other computers.");
      break;
    case Kind::ScreenRecording:
      title = tr("Screen Recording");
      purpose =
          tr("For screenshots that show other apps' windows, and for letting the server see this screen "
             "(Remote Control).");
      break;
    }

    auto *control = new QWidget();
    auto *controlLayout = new QHBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(8);

    QString hint;
    QPushButton *action = nullptr;
    switch (m_monitor->state(kind)) {
    case State::Granted:
      controlLayout->addWidget(pill(tr("Allowed"), "ok"));
      break;
    case State::NotGranted:
      controlLayout->addWidget(pill(tr("Not allowed"), "off"));
      action = new QPushButton(tr("Allow…"));
      hint = tr("Already switched on in System Settings? That entry is from an earlier build; Allow replaces it.");
      break;
    case State::Stale:
      controlLayout->addWidget(pill(tr("Needs repair"), "warn"));
      action = new QPushButton(tr("Repair…"));
      hint = tr("The switch in System Settings belongs to an earlier build and no longer counts.");
      break;
    case State::RestartNeeded: {
      controlLayout->addWidget(pill(tr("Restart needed"), "busy"));
      auto *restart = new QPushButton(tr("Restart %1").arg(kAppName));
      restart->setProperty("primary", true);
      connect(restart, &QPushButton::clicked, m_monitor, &PermissionMonitor::relaunch);
      controlLayout->addWidget(restart);
      hint = tr("Switch it on in System Settings, then restart: macOS applies it on the next start.");
      break;
    }
    }
    if (action) {
      action->setProperty("primary", true);
      connect(action, &QPushButton::clicked, this, [this, kind] { m_monitor->allow(kind); });
      controlLayout->addWidget(action);
    }

    const auto row = card->addRow(title, hint.isEmpty() ? purpose : purpose + QStringLiteral("\n") + hint, control);
    row.detail->setVisible(true);
  }
  page->addWidget(card);

  page->addSection(tr("If something still doesn't work"));
  auto *help = new FormCard();
  if (m_monitor->stableSignature()) {
    help->addRow(
        tr("Updates keep permissions"),
        tr("This build is signed with “%1”, so macOS recognises later versions as the same app.")
            .arg(m_monitor->signer())
    );
  } else {
    help->addRow(
        tr("Updates may need repairs"),
        tr("This build has no stable signature, so macOS treats each new version as a different app. After an "
           "update, use Repair above.")
    );
  }

  auto *restart = new QPushButton(tr("Restart"));
  connect(restart, &QPushButton::clicked, m_monitor, &PermissionMonitor::relaunch);
  help->addRow(
      tr("Restart %1").arg(kAppName), tr("Some permissions only take effect when the app starts again."), restart
  );

  auto *startOver = new QPushButton(tr("Start Over…"));
  startOver->setProperty("destructive", true);
  connect(startOver, &QPushButton::clicked, this, [this] {
    const auto answer = QMessageBox::question(
        this, tr("Start over with permissions?"),
        tr("This removes every %1 entry from Privacy & Security and restarts %1. You then allow each permission "
           "again from this page.")
            .arg(kAppName),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel
    );
    if (answer == QMessageBox::Yes)
      m_monitor->resetAll();
  });
  help->addRow(
      tr("Start over"),
      tr("Removes all of %1's entries, including duplicates left by older builds, and asks again from scratch.")
          .arg(kAppName),
      startOver
  );
  page->addWidget(help);
}

} // namespace deskflow::gui::pages
