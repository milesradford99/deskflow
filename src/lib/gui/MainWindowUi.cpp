/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindowUi.h"

#include "Theme.h"
#include "common/Constants.h"
#include "common/PlatformInfo.h"
#include "widgets/MacControls.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace deskflow::gui;
using namespace deskflow::gui::mac;

namespace Ui {

namespace {

QPushButton *navButton(QWidget *parent)
{
  auto *button = new QPushButton(parent);
  button->setProperty("nav", true);
  button->setCheckable(true);
  button->setAutoExclusive(true);
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::NoFocus);
  button->setIconSize(QSize(18, 18));
  return button;
}

} // namespace

void MainWindow::setupUi(QMainWindow *window)
{
  window->setObjectName(QStringLiteral("MainWindow"));
  window->setMinimumSize(760, 520);
  window->resize(940, 640);

  central = new QWidget(window);
  auto *shell = new QHBoxLayout(central);
  shell->setContentsMargins(0, 0, 0, 0);
  shell->setSpacing(0);

  // --- sidebar -------------------------------------------------------------
  sidebar = new QFrame(central);
  sidebar->setObjectName(QStringLiteral("sidebar"));
  sidebar->setFixedWidth(220);
  auto *side = new QVBoxLayout(sidebar);
  side->setContentsMargins(12, 18, 12, 12);
  side->setSpacing(2);

  auto *brand = new QHBoxLayout();
  brand->setContentsMargins(6, 0, 0, 0);
  brand->setSpacing(10);
  brandIcon = new QLabel(sidebar);
  brandIcon->setFixedSize(34, 34);
  brand->addWidget(brandIcon);
  auto *brandText = new QVBoxLayout();
  brandText->setSpacing(0);
  brandName = label(kAppName, "brand", sidebar);
  brandTagline = label(QString(), "tertiary", sidebar);
  brandTagline->setWordWrap(true);
  brandText->addWidget(brandName);
  brandText->addWidget(brandTagline);
  brand->addLayout(brandText, 1);
  side->addLayout(brand);
  side->addSpacing(18);

  navHome = navButton(sidebar);
  navClipboard = navButton(sidebar);
  navScreenshot = navButton(sidebar);
  navOverlay = navButton(sidebar);
  navLog = navButton(sidebar);
  for (auto *button : {navHome, navClipboard, navScreenshot, navOverlay, navLog})
    side->addWidget(button);
  side->addStretch(1);

  statusCard = new QFrame(sidebar);
  statusCard->setProperty("sideCard", true);
  auto *statusLayout = new QVBoxLayout(statusCard);
  statusLayout->setContentsMargins(12, 10, 12, 10);
  statusLayout->setSpacing(2);
  auto *statusTop = new QHBoxLayout();
  statusTop->setSpacing(7);
  statusDot = new QLabel(statusCard);
  statusDot->setFixedSize(8, 8);
  statusDot->setProperty("dot", true);
  statusTop->addWidget(statusDot, 0, Qt::AlignVCenter);
  statusText = label(QString(), nullptr, statusCard);
  statusTop->addWidget(statusText, 1);
  statusLayout->addLayout(statusTop);
  statusDetail = label(QString(), "tertiary", statusCard);
  statusDetail->setWordWrap(true);
  statusLayout->addWidget(statusDetail);
  side->addWidget(statusCard);
  side->addSpacing(8);

  navSettings = navButton(sidebar);
  side->addWidget(navSettings);
  shell->addWidget(sidebar);

  // --- content ---------------------------------------------------------------
  auto *content = new QWidget(central);
  contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  pages = new QStackedWidget(content);
  pages->setObjectName(QStringLiteral("pages"));
  contentLayout->addWidget(pages, 1);
  shell->addWidget(content, 1);

  // --- "This Computer" -------------------------------------------------------
  homePage = new Page(pages);
  homeLayout = homePage->contentLayout();

  // the title is the computer's name, editable in place
  lblComputerName = homePage->titleLabel();
  lineEditName = new QLineEdit(homePage);
  lineEditName->setProperty("titleEdit", true);
  lineEditName->setMaxLength(255);
  lineEditName->setVisible(false);
  homePage->titleRow()->insertWidget(1, lineEditName, 1);
  btnEditName = new QPushButton(homePage);
  btnEditName->setProperty("iconOnly", true);
  btnEditName->setCursor(Qt::PointingHandCursor);
  btnEditName->setIconSize(QSize(16, 16));
  homePage->titleRow()->insertWidget(2, btnEditName, 0, Qt::AlignVCenter);
  lblIpAddresses = homePage->subtitleLabel();
  lblIpAddresses->setVisible(true);
  lblIpAddresses->setTextInteractionFlags(Qt::TextSelectableByMouse);

  lblRole = homePage->addSection(QString());
  auto *roles = new QHBoxLayout();
  roles->setSpacing(10);
  rbModeServer = new ChoiceCard(Symbol::Server, homePage);
  rbModeClient = new ChoiceCard(Symbol::Client, homePage);
  roles->addWidget(rbModeServer, 1);
  roles->addWidget(rbModeClient, 1);
  homePage->addLayout(roles);

  lblSetup = homePage->addSection(QString());

  lblNoMode = label(QString(), "secondary", homePage);
  lblNoMode->setAlignment(Qt::AlignCenter);
  lblNoMode->setMinimumHeight(56);
  homePage->addWidget(lblNoMode);

  // server: arrange screens
  serverOptions = new FormCard(homePage);
  auto *layoutButtons = new QWidget();
  auto *layoutButtonsLayout = new QHBoxLayout(layoutButtons);
  layoutButtonsLayout->setContentsMargins(0, 0, 0, 0);
  layoutButtonsLayout->setSpacing(6);
  btnSaveServerConfig = new QPushButton(layoutButtons);
  btnSaveServerConfig->setProperty("iconOnly", true);
  btnSaveServerConfig->setIconSize(QSize(18, 18));
  btnConfigureServer = new QPushButton(layoutButtons);
  layoutButtonsLayout->addWidget(btnSaveServerConfig);
  layoutButtonsLayout->addWidget(btnConfigureServer);
  const auto layoutRow = serverOptions->addRow(QString(), QString(), layoutButtons);
  lblLayoutTitle = layoutRow.title;
  lblLayoutDetail = layoutRow.detail;
  lblLayoutDetail->setVisible(true);
  homePage->addWidget(serverOptions);

  // client: where the server is
  clientOptions = new FormCard(homePage);
  cbHostname = new QComboBox();
  cbHostname->setEditable(true);
  cbHostname->setInsertPolicy(QComboBox::NoInsert);
  cbHostname->setMinimumWidth(240);
  const auto hostRow = clientOptions->addRow(QString(), QString(), cbHostname);
  m_pLabelServerName = hostRow.title;
  lblHostDetail = hostRow.detail;
  lblHostDetail->setVisible(true);
  serverStatus = label(QString(), "rowDetail");
  serverStatus->setWordWrap(true);
  serverStatus->setVisible(false);
  hostRow.detail->parentWidget()->layout()->itemAt(0)->layout()->addWidget(serverStatus);
  btnConfigureClient = new QPushButton();
  const auto optionsRow = clientOptions->addRow(QString(), QString(), btnConfigureClient);
  lblClientOptionsTitle = optionsRow.title;
  lblClientOptionsDetail = optionsRow.detail;
  lblClientOptionsDetail->setVisible(true);
  homePage->addWidget(clientOptions);

  // start / stop
  actionRow = new QWidget(homePage);
  auto *action = new QHBoxLayout(actionRow);
  action->setContentsMargins(0, 8, 0, 0);
  action->setSpacing(8);
  btnToggleCore = new QPushButton(actionRow);
  btnToggleCore->setProperty("primary", true);
  btnToggleCore->setProperty("large", true);
  btnToggleCore->setEnabled(false);
  btnToggleCore->setIconSize(QSize(14, 14));
  btnToggleCore->setCursor(Qt::PointingHandCursor);
  btnRestartCore = new QPushButton(actionRow);
  btnRestartCore->setProperty("iconOnly", true);
  btnRestartCore->setIconSize(QSize(18, 18));
  btnRestartCore->setEnabled(false);
  lblActionHint = label(QString(), "secondary", actionRow);
  lblActionHint->setWordWrap(true);
  action->addWidget(btnToggleCore);
  action->addWidget(btnRestartCore);
  action->addSpacing(8);
  action->addWidget(lblActionHint, 1);
  homePage->addWidget(actionRow);

  homeLayout->addSpacing(10);

  pages->insertWidget(HomePage, homePage);

  window->setCentralWidget(central);
  retranslateUi(window);
  updateIcons();
}

void MainWindow::updateIcons()
{
  const QColor secondary = theme::textSecondary();
  const QColor white(Qt::white);

  navHome->setIcon(symbol(Symbol::Computers, secondary, white));
  navClipboard->setIcon(symbol(Symbol::Clipboard, secondary, white));
  navScreenshot->setIcon(symbol(Symbol::Screenshot, secondary, white));
  navOverlay->setIcon(symbol(Symbol::Sparkle, secondary, white));
  navLog->setIcon(symbol(Symbol::Log, secondary, white));
  navSettings->setIcon(symbol(Symbol::Settings, secondary, white));

  btnEditName->setIcon(symbol(Symbol::Pencil, secondary));
  btnSaveServerConfig->setIcon(symbol(Symbol::Export, secondary));
  btnRestartCore->setIcon(symbol(Symbol::Restart, secondary));
  btnConfigureServer->setIcon(QIcon());

  brandIcon->setPixmap(QIcon::fromTheme(kRevFqdnName).pixmap(QSize(34, 34)));
}

void MainWindow::retranslateUi(QMainWindow *window)
{
  Q_UNUSED(window)
  brandTagline->setText(tr("One keyboard and mouse"));

  navHome->setText(tr("This Computer"));
  navClipboard->setText(tr("Clipboard"));
  navScreenshot->setText(tr("Screenshot"));
  navOverlay->setText(tr("Screen Hints"));
  navLog->setText(tr("Log"));
  const auto settingsShortcut = deskflow::platform::isMac() ? QStringLiteral("  ⌘,") : QString();
  navSettings->setText(tr("Settings") + settingsShortcut);

  btnEditName->setToolTip(tr("Rename this computer"));
  lineEditName->setToolTip(tr("Letters, numbers, - and _ only. Press Return to save or Esc to cancel."));

  lblRole->setText(tr("What should this computer do?"));
  rbModeServer->setTitle(tr("Share this keyboard and mouse"));
  rbModeServer->setDescription(tr("Control your other computers from this one. It becomes the server."));
  rbModeClient->setTitle(tr("Use another computer's keyboard and mouse"));
  rbModeClient->setDescription(tr("Another computer controls this one. It becomes a client."));
  lblNoMode->setText(tr("Choose one of the two above to get started."));
  lblSetup->setText(tr("Set up"));

  lblLayoutTitle->setText(tr("Screen layout"));
  lblLayoutDetail->setText(tr("Place your other computers around this one, the way they sit on your desk."));
  btnConfigureServer->setText(tr("Arrange Screens…"));
  btnSaveServerConfig->setToolTip(tr("Export server configuration"));

  m_pLabelServerName->setText(tr("Server"));
  lblHostDetail->setText(tr("The name or IP address shown on the computer whose keyboard you want to use."));
  cbHostname->setToolTip(
      tr("<html>Hostname or IP address of the server computer.<br/>May contain a comma seperated list.</html>")
  );
  if (cbHostname->lineEdit())
    cbHostname->lineEdit()->setPlaceholderText(tr("e.g. 192.168.1.10"));
  lblClientOptionsTitle->setText(tr("Client options"));
  lblClientOptionsDetail->setText(tr("Other servers, scrolling and reconnecting."));
  btnConfigureClient->setText(tr("Options…"));

  btnRestartCore->setToolTip(tr("Restart"));
}

} // namespace Ui
