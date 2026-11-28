/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Builds that can't capture the screen (e.g. Wayland only): the viewer says so.

#include "remoteview/ScreenCapturer.h"

namespace deskflow::remote {

std::unique_ptr<ScreenCapturer> createScreenCapturer(const QString &)
{
  return nullptr;
}

} // namespace deskflow::remote
