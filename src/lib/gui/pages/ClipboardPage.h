/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

#include <QPointer>

class QLabel;

namespace deskflow::gui::clipboard {
class ClipboardHistory;
}

namespace deskflow::gui::pages {

//! Clipboard history: open it, and every option that used to hide in its gear menu.
class ClipboardPage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit ClipboardPage(clipboard::ClipboardHistory *history, QWidget *parent = nullptr);

protected:
  void build(mac::Page *page) override;

private:
  void updateState();
  clipboard::ClipboardHistory *m_history;
  QPointer<QLabel> m_shortcut;
  QPointer<QLabel> m_count;
};

} // namespace deskflow::gui::pages
