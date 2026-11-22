/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OverlayPage.h"

#include "common/Settings.h"
#include "gui/widgets/MacControls.h"

#include <QHBoxLayout>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;

OverlayPage::OverlayPage(QWidget *parent) : RebuildablePage(parent)
{
  // nothing else to set up; the page is built when shown
}

void OverlayPage::build(Page *page)
{
  page->setTitle(tr("Screen Hints"));
  page->setSubtitle(
      tr("Small hints on screen show which computer your keys and cursor are on. They fade away on their own.")
  );

  page->addSection(tr("Show on screen"));
  auto *card = new FormCard();

  // a switch with a "Try it" button beside it; the button only makes sense while the switch is on
  const auto addHint =
      [this, card](const QString &key, const QString &title, const QString &detail, void (OverlayPage::*preview)()) {
        auto *controls = new QWidget();
        auto *layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        auto *tryIt = new QPushButton(tr("Try It"));
        tryIt->setProperty("link", true);
        auto *toggle = new ToggleSwitch();
        toggle->setChecked(Settings::value(key).toBool());
        tryIt->setEnabled(toggle->isChecked());
        layout->addWidget(tryIt);
        layout->addWidget(toggle);
        connect(toggle, &ToggleSwitch::toggled, this, [key, tryIt](bool on) {
          Settings::setValue(key, on);
          tryIt->setEnabled(on);
        });
        connect(tryIt, &QPushButton::clicked, this, preview);
        card->addRow(title, detail, controls);
      };

  addHint(
      Settings::Gui::OverlayShowKeys, tr("Keys you press"),
      tr("Each key shows for a moment in the top-right corner of the computer you are typing on. "
         "Nothing is recorded."),
      &OverlayPage::previewKeys
  );
  addHint(
      Settings::Gui::OverlayShowActivity, tr("What Deskflow is doing"),
      tr("Switching computers, sharing the clipboard, computers connecting and leaving."), &OverlayPage::previewActivity
  );
  addHint(
      Settings::Gui::CursorHighlight, tr("Where the cursor arrives"),
      tr("A short ripple around the cursor when it comes to this computer, so it is easy to find."),
      &OverlayPage::previewCursor
  );
  page->addWidget(card);
}

} // namespace deskflow::gui::pages
