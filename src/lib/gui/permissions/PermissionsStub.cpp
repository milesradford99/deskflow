/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Platforms without per-app privacy permissions: everything is allowed.

#include "Permissions.h"

namespace deskflow::gui::permissions::platform {

bool supported()
{
  return false;
}

bool granted(Kind)
{
  return true;
}

void prompt(Kind)
{
  // nothing to ask for
}

bool reset(Kind)
{
  return false;
}

bool resetAll()
{
  return false;
}

void openSettings(Kind)
{
  // no settings pane
}

QString signature()
{
  return {};
}

bool stableSignature()
{
  return false;
}

QString signer()
{
  return {};
}

bool scheduleRelaunch(qint64)
{
  return false;
}

} // namespace deskflow::gui::permissions::platform
