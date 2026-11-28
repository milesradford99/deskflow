/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

#include <QPointer>

class QLabel;

namespace deskflow::gui::screenshot {
class ScreenshotTool;
}

namespace deskflow::gui::pages {

//! Screenshot tool: take one, its shortcut and what happens after a capture.
class ScreenshotPage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit ScreenshotPage(screenshot::ScreenshotTool *tool, QWidget *parent = nullptr);

protected:
  void build(mac::Page *page) override;

private:
  void updateState();
  screenshot::ScreenshotTool *m_tool;
  QPointer<QLabel> m_shortcut;
};

} // namespace deskflow::gui::pages
