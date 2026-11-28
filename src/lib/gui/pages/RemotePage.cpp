/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RemotePage.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/remote/RemoteViewManager.h"
#include "gui/widgets/MacControls.h"
#include "remoteview/RemoteFrame.h"

#include <QComboBox>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;
using deskflow::gui::remote::RemoteViewManager;

RemotePage::RemotePage(RemoteViewManager *manager, QWidget *parent) : RebuildablePage(parent), m_manager{manager}
{
  connect(m_manager, &RemoteViewManager::clientsChanged, this, &RemotePage::scheduleRebuild);
}

void RemotePage::build(Page *page)
{
  page->setTitle(tr("Remote Control"));
  page->setSubtitle(
      tr("See a linked computer's screen in a window on this one and use it with this keyboard and mouse, "
         "without looking at its own display. It uses the same connection as sharing the keyboard and mouse.")
  );

  // computers this server can look at
  page->addSection(tr("Computers"));
  auto *computers = new FormCard();
  const auto clients = m_manager->clients();
  const bool isServer =
      Settings::value(Settings::Core::CoreMode).toInt() == static_cast<int>(Settings::CoreMode::Server);
  if (clients.isEmpty()) {
    computers->addRow(
        isServer ? tr("No computers connected yet") : tr("Only on the server"),
        isServer ? tr("Computers appear here once they connect to this one.")
                 : tr("Use this computer as the server to see and control the computers linked to it.")
    );
  }
  for (const auto &client : clients) {
    auto *view = new QPushButton(tr("View Screen"));
    view->setProperty("primary", true);
    connect(view, &QPushButton::clicked, this, [this, client] { m_manager->open(client); });
    computers->addRow(client, tr("Connected"), view);
  }
  page->addWidget(computers);

  // how the picture looks
  page->addSection(tr("Picture"));
  auto *picture = new FormCard();
  auto *quality = new SegmentedControl();
  quality->addSegment(tr("Smooth"));
  quality->addSegment(tr("Balanced"));
  quality->addSegment(tr("Sharp"));
  quality->setCurrentIndex(std::clamp(Settings::value(Settings::RemoteView::Quality).toInt(), 0, 2));
  connect(quality, &SegmentedControl::currentChanged, this, [](int index) {
    Settings::setValue(Settings::RemoteView::Quality, index);
  });
  picture->addRow(
      tr("Quality"), tr("Smooth sends a smaller picture. Sharp sends full resolution (up to 4K), best for small text."),
      quality
  );

  auto *rate = new QComboBox();
  const int current = Settings::value(Settings::RemoteView::FrameRate).toInt();
  for (const int fps : {15, 30, 60}) {
    rate->addItem(tr("%1 frames per second").arg(fps), fps);
    if (fps == current)
      rate->setCurrentIndex(rate->count() - 1);
  }
  connect(rate, &QComboBox::currentIndexChanged, this, [rate] {
    Settings::setValue(Settings::RemoteView::FrameRate, rate->currentData());
  });
  picture->addRow(tr("Frame rate"), tr("The most it sends; a still screen sends almost nothing."), rate);

  picture->addRow(
      tr("Video"),
      deskflow::remote::FrameCanvas::canDecodeH264()
          ? tr("H.264 with the graphics hardware where both computers have it, otherwise compressed images.")
          : tr("Compressed images (this build has no H.264 decoder).")
  );
  page->addWidget(picture);

  // being looked at
  page->addSection(tr("This Computer"));
  auto *self = new FormCard();
  auto *allow = new ToggleSwitch();
  allow->setChecked(Settings::value(Settings::RemoteView::AllowViewing).toBool());
  connect(allow, &ToggleSwitch::toggled, this, [](bool on) {
    Settings::setValue(Settings::RemoteView::AllowViewing, on);
  });
#ifdef Q_OS_MACOS
  const auto allowDetail = tr("When this computer is a client, its server may show its screen and control it. The "
                              "first time, macOS asks to allow screen recording for %1.")
                               .arg(kAppName);
#else
  const auto allowDetail = tr("When this computer is a client, its server may show its screen and control it.");
#endif
  self->addRow(tr("Allow remote viewing"), allowDetail, allow);
  self->addRow(
      tr("While someone is watching"),
      tr("A notice appears in the corner of this screen when a viewer opens or closes.")
  );
  page->addWidget(self);
}

} // namespace deskflow::gui::pages
