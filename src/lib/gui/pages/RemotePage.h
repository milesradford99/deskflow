/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

namespace deskflow::gui::remote {
class RemoteViewManager;
}

namespace deskflow::gui::pages {

//! Remote control: see a connected computer's screen in a window and use it from here.
class RemotePage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit RemotePage(remote::RemoteViewManager *manager, QWidget *parent = nullptr);

protected:
  void build(mac::Page *page) override;

private:
  remote::RemoteViewManager *m_manager;
};

} // namespace deskflow::gui::pages
