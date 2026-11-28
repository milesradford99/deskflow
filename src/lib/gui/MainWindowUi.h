/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QCoreApplication>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QMainWindow;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace deskflow::gui::mac {
class ChoiceCard;
class FormCard;
class Page;
} // namespace deskflow::gui::mac

namespace Ui {

/**
 * @brief The main window's widgets, built in code.
 *
 * Layout, after macOS apps such as System Settings and the owner's DayOfFlow:
 * a sidebar (brand, sections, status, Settings) and a stack of pages. The first
 * page is "This Computer": its name, its role as two choice cards, what that
 * role needs, the start button and sending files.
 */
class MainWindow
{
  Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
  enum PageIndex
  {
    HomePage,
    ClipboardPage,
    ScreenshotPage,
    OverlayPage,
    LogPage,
    SettingsPage
  };

  void setupUi(QMainWindow *window);
  void retranslateUi(QMainWindow *window);

  //! Re-tint the sidebar and button symbols after a light/dark switch.
  void updateIcons();

  // shell
  QWidget *central = nullptr;
  QFrame *sidebar = nullptr;
  QLabel *brandIcon = nullptr;
  QLabel *brandName = nullptr;
  QLabel *brandTagline = nullptr;
  QPushButton *navHome = nullptr;
  QPushButton *navClipboard = nullptr;
  QPushButton *navScreenshot = nullptr;
  QPushButton *navOverlay = nullptr;
  QPushButton *navLog = nullptr;
  QPushButton *navSettings = nullptr;
  QFrame *statusCard = nullptr;
  QLabel *statusDot = nullptr;
  QLabel *statusText = nullptr;
  QLabel *statusDetail = nullptr;
  QStackedWidget *pages = nullptr;
  QVBoxLayout *contentLayout = nullptr;

  // "This Computer" page
  deskflow::gui::mac::Page *homePage = nullptr;
  QLabel *lblComputerName = nullptr;
  QLineEdit *lineEditName = nullptr;
  QPushButton *btnEditName = nullptr;
  QLabel *lblIpAddresses = nullptr;
  QLabel *lblRole = nullptr;
  deskflow::gui::mac::ChoiceCard *rbModeServer = nullptr;
  deskflow::gui::mac::ChoiceCard *rbModeClient = nullptr;
  QLabel *lblNoMode = nullptr;
  QLabel *lblSetup = nullptr;

  deskflow::gui::mac::FormCard *serverOptions = nullptr;
  QLabel *lblLayoutTitle = nullptr;
  QLabel *lblLayoutDetail = nullptr;
  QPushButton *btnConfigureServer = nullptr;
  QPushButton *btnSaveServerConfig = nullptr;

  deskflow::gui::mac::FormCard *clientOptions = nullptr;
  QLabel *m_pLabelServerName = nullptr;
  QLabel *lblHostDetail = nullptr;
  QComboBox *cbHostname = nullptr;
  QLabel *lblClientOptionsTitle = nullptr;
  QLabel *lblClientOptionsDetail = nullptr;
  QPushButton *btnConfigureClient = nullptr;
  //! Client with several servers: one line per server's state
  QLabel *serverStatus = nullptr;

  QWidget *actionRow = nullptr;
  QPushButton *btnToggleCore = nullptr;
  QPushButton *btnRestartCore = nullptr;
  QLabel *lblActionHint = nullptr;

  QVBoxLayout *homeLayout = nullptr;
};

} // namespace Ui
