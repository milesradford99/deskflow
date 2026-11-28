/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

namespace deskflow::gui::permissions {
class PermissionMonitor;
}

namespace deskflow::gui::pages {

/**
 * @brief macOS privacy permissions: what each one is for, whether Deskflow has it right now,
 * and one button that fixes it (allow, repair a stale entry, or restart).
 */
class PermissionsPage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit PermissionsPage(permissions::PermissionMonitor *monitor, QWidget *parent = nullptr);

protected:
  void build(mac::Page *page) override;

private:
  permissions::PermissionMonitor *m_monitor;
};

} // namespace deskflow::gui::pages
