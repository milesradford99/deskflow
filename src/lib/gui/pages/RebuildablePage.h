/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

namespace deskflow::gui::mac {
class Page;
}

namespace deskflow::gui::pages {

/**
 * @brief A main window page whose controls are read from Settings when built.
 *
 * Changes are written straight to Settings (no Save button), so the page can be
 * rebuilt at any time: on a language change, or when settings change elsewhere.
 */
class RebuildablePage : public QWidget
{
  Q_OBJECT
public:
  explicit RebuildablePage(QWidget *parent = nullptr);

  //! Rebuild on the next turn of the event loop (safe from inside a control's signal).
  void scheduleRebuild();

protected:
  //! Fill a fresh page.
  virtual void build(mac::Page *page) = 0;
  void rebuild();
  void changeEvent(QEvent *event) override;
  //! Re-read settings each time the page is shown, so changes made elsewhere show up.
  void showEvent(QShowEvent *event) override;
  mac::Page *page() const
  {
    return m_page;
  }

private:
  mac::Page *m_page = nullptr;
  bool m_rebuildPending = false;
};

} // namespace deskflow::gui::pages
