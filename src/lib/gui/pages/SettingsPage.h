/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "RebuildablePage.h"

#include <QPointer>

class QFrame;

namespace deskflow::gui::pages {

/**
 * @brief Preferences as a page of the main window.
 *
 * Every change applies at once, like System Settings: there is no Save, Reset or
 * Cancel. Settings the running core reads at start-up raise a banner that offers
 * to restart sharing, instead of restarting it behind the user's back.
 */
class SettingsPage : public RebuildablePage
{
  Q_OBJECT
public:
  explicit SettingsPage(QWidget *parent = nullptr);

  //! Show or hide the "restart to apply" banner.
  void setRestartNeeded(bool needed);

  //! Keys the core only reads when it starts.
  static bool needsCoreRestart(const QString &key);

Q_SIGNALS:
  void restartRequested();
  void requestRemoveAllSettings();
  void requestShowFingerprint();

protected:
  void build(mac::Page *page) override;

private:
  void buildGeneral(mac::Page *page);
  void buildNetwork(mac::Page *page);
  void buildAdvanced(mac::Page *page);
  void resetAllSettings();

  int m_tab = 0;
  bool m_restartNeeded = false;
  QPointer<QFrame> m_banner;
};

} // namespace deskflow::gui::pages
