/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

namespace deskflow::gui::pages {

//! On-screen hints: key presses, Deskflow events and the cursor ripple, each with a preview.
class OverlayPage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit OverlayPage(QWidget *parent = nullptr);

Q_SIGNALS:
  void previewKeys();
  void previewActivity();
  void previewCursor();

protected:
  void build(mac::Page *page) override;
};

} // namespace deskflow::gui::pages
