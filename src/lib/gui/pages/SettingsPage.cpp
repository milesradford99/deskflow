/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsPage.h"

#include "common/I18N.h"
#include "common/LogLevel.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "gui/TlsUtility.h"
#include "gui/core/NetworkMonitor.h"
#include "gui/widgets/MacControls.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace deskflow::gui::pages {

using namespace deskflow::gui::mac;

namespace {

//! A switch bound to a boolean setting; @p inverted shows the opposite of the stored value.
ToggleSwitch *settingSwitch(const QString &key, bool inverted = false)
{
  auto *toggle = new ToggleSwitch();
  toggle->setChecked(Settings::value(key).toBool() != inverted);
  QObject::connect(toggle, &ToggleSwitch::toggled, toggle, [key, inverted](bool on) {
    Settings::setValue(key, on != inverted);
  });
  return toggle;
}

//! A text field that saves when editing finishes (Return or leaving the field).
QLineEdit *settingLine(const QString &key, const QString &placeholder = QString())
{
  auto *line = new QLineEdit(Settings::value(key).toString());
  line->setPlaceholderText(placeholder);
  QObject::connect(line, &QLineEdit::editingFinished, line, [key, line] { Settings::setValue(key, line->text()); });
  return line;
}

QString elidedPath(const QString &path)
{
  if (path.isEmpty())
    return QString();
  const QFileInfo info(path);
  return QStringLiteral("…/%1/%2").arg(info.dir().dirName(), info.fileName());
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent) : RebuildablePage(parent)
{
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, [this] {
    if (isVisible())
      scheduleRebuild();
  });
}

bool SettingsPage::needsCoreRestart(const QString &key)
{
  static const QStringList keys{
      Settings::Core::Port,
      Settings::Core::Interface,
      Settings::Core::PreventSleep,
      Settings::Core::ProcessMode,
      Settings::Core::EnableEnterCommand,
      Settings::Core::EnableExitCommand,
      Settings::Core::ComputerEnterCommand,
      Settings::Core::ComputerExitCommand,
      Settings::Daemon::Elevate,
      Settings::Log::ToFile,
      Settings::Log::File,
      Settings::Security::TlsEnabled,
      Settings::Security::Certificate,
      Settings::Security::KeySize,
      Settings::Security::CheckPeers,
  };
  return keys.contains(key);
}

void SettingsPage::setRestartNeeded(bool needed)
{
  m_restartNeeded = needed;
  if (m_banner)
    m_banner->setVisible(needed);
}

void SettingsPage::build(Page *page)
{
  page->setTitle(tr("Settings"));
  page->setSubtitle(tr("Changes apply right away."));

  // restart banner
  m_banner = new QFrame();
  m_banner->setProperty("banner", true);
  auto *bannerLayout = new QHBoxLayout(m_banner);
  bannerLayout->setContentsMargins(14, 8, 8, 8);
  auto *bannerText = label(tr("Some changes take effect when sharing restarts."), nullptr);
  bannerText->setWordWrap(true);
  bannerLayout->addWidget(bannerText, 1);
  auto *restart = new QPushButton(tr("Restart Now"));
  restart->setProperty("primary", true);
  connect(restart, &QPushButton::clicked, this, [this] {
    setRestartNeeded(false);
    Q_EMIT restartRequested();
  });
  bannerLayout->addWidget(restart);
  m_banner->setVisible(m_restartNeeded);
  page->addWidget(m_banner);

  if (!Settings::isWritable()) {
    auto *readOnly = label(tr("These settings are read-only: the settings file can't be written."), "error");
    readOnly->setWordWrap(true);
    page->addWidget(readOnly);
  }

  auto *tabs = new SegmentedControl();
  tabs->addSegment(tr("General"));
  tabs->addSegment(tr("Network && Security"));
  tabs->addSegment(tr("Advanced"));
  tabs->setCurrentIndex(m_tab);
  connect(tabs, &SegmentedControl::currentChanged, this, [this](int index) {
    m_tab = index;
    scheduleRebuild();
  });
  auto *tabRow = new QHBoxLayout();
  tabRow->addWidget(tabs);
  tabRow->addStretch(1);
  page->addLayout(tabRow);

  switch (m_tab) {
  case 1:
    buildNetwork(page);
    break;
  case 2:
    buildAdvanced(page);
    break;
  default:
    buildGeneral(page);
    break;
  }

  // nothing can be changed while the settings file is read-only
  if (!Settings::isWritable()) {
    for (auto *card : page->findChildren<FormCard *>())
      card->setEnabled(false);
  }
}

void SettingsPage::buildGeneral(Page *page)
{
  const bool mac = deskflow::platform::isMac();

  page->addSection(tr("Window"));
  auto *window = new FormCard();
  window->addRow(tr("Open this window when Deskflow starts"), QString(), settingSwitch(Settings::Gui::Autohide, true));
  window->addRow(
      tr("Keep sharing when the window is closed"),
      mac ? tr("Deskflow stays in the menu bar. Quit it from there.")
          : tr("Deskflow stays in the system tray. Quit it from there."),
      settingSwitch(Settings::Gui::CloseToTray)
  );

  auto *iconStyle = new SegmentedControl();
  iconStyle->addSegment(tr("Colorful"));
  iconStyle->addSegment(tr("Monochrome"));
  iconStyle->setCurrentIndex(Settings::value(Settings::Gui::SymbolicTrayIcon).toBool() ? 1 : 0);
  connect(iconStyle, &SegmentedControl::currentChanged, this, [](int index) {
    Settings::setValue(Settings::Gui::SymbolicTrayIcon, index == 1);
  });
  window->addRow(mac ? tr("Menu bar icon") : tr("Tray icon"), QString(), iconStyle);
  window->addRow(tr("Show the version in the title bar"), QString(), settingSwitch(Settings::Gui::ShowVersionInTitle));
  page->addWidget(window);

  page->addSection(tr("General"));
  auto *general = new FormCard();

  auto *language = new QComboBox();
  I18N::reDetectLanguages();
  language->addItems(I18N::detectedLanguages());
  language->setCurrentText(I18N::toNativeName(I18N::currentLanguage()));
  connect(language, &QComboBox::currentTextChanged, this, [](const QString &name) {
    const auto shortName = I18N::nativeTo639Name(name);
    Settings::setValue(Settings::Core::Language, shortName);
    I18N::setLanguage(shortName);
  });
  general->addRow(tr("Language"), QString(), language);

  general->addRow(
      tr("Keep this computer awake"), tr("Stops it from going to sleep while Deskflow is sharing."),
      settingSwitch(Settings::Core::PreventSleep)
  );
  general->addRow(
      tr("Check for updates when Deskflow starts"), QString(), settingSwitch(Settings::Gui::AutoUpdateCheck)
  );
  page->addWidget(general);
}

void SettingsPage::buildNetwork(Page *page)
{
  const bool isServer = Settings::value(Settings::Core::CoreMode) == Settings::CoreMode::Server;

  page->addSection(tr("Network"));
  auto *network = new FormCard();

  auto *address = new QComboBox();
  address->addItem(tr("Automatic"), QString());
  for (const auto &ip : NetworkMonitor::validAddresses())
    address->addItem(ip, ip);
  const auto current = Settings::value(Settings::Core::Interface).toString();
  if (!current.isEmpty() && address->findData(current) < 0)
    address->addItem(current, current);
  address->setCurrentIndex(qMax(0, address->findData(current)));
  connect(address, &QComboBox::currentIndexChanged, this, [address] {
    const auto ip = address->currentData().toString();
    Settings::setValue(Settings::Core::Interface, ip.isEmpty() ? QVariant() : QVariant(ip));
  });
  network->addRow(
      tr("IP address"), tr("Automatic is right for most networks. Pick one if this computer is on several."), address
  );

  auto *port = new QLineEdit(QString::number(Settings::value(Settings::Core::Port).toInt()));
  port->setValidator(new QIntValidator(1, 65535, port));
  port->setAlignment(Qt::AlignRight);
  port->setFixedWidth(90);
  connect(port, &QLineEdit::editingFinished, this, [port] {
    if (port->hasAcceptableInput())
      Settings::setValue(Settings::Core::Port, port->text().toInt());
  });
  network->addRow(tr("Port"), tr("Every computer must use the same port."), port);
  page->addWidget(network);

  page->addSection(tr("Security"));
  auto *security = new FormCard();
  const bool tls = Settings::value(Settings::Security::TlsEnabled).toBool();

  auto *tlsSwitch = settingSwitch(Settings::Security::TlsEnabled);
  connect(tlsSwitch, &ToggleSwitch::toggled, this, &SettingsPage::scheduleRebuild);
  security->addRow(
      tr("Encrypt connections"), tr("Recommended. Turn it on or off on every computer at the same time."), tlsSwitch
  );

  if (tls) {
    auto *keyLength = new SegmentedControl();
    keyLength->addSegment(tr("2048-bit"));
    keyLength->addSegment(tr("4096-bit"));
    keyLength->setCurrentIndex(Settings::value(Settings::Security::KeySize).toInt() == 4096 ? 1 : 0);
    connect(keyLength, &SegmentedControl::currentChanged, this, [](int index) {
      Settings::setValue(Settings::Security::KeySize, index == 1 ? 4096 : 2048);
    });
    security->addRow(tr("Key length"), tr("Longer keys are stronger but take a moment to create."), keyLength);

    const auto certificate = Settings::value(Settings::Security::Certificate).toString();
    QString certDetail = elidedPath(certificate);
    if (QFile::exists(certificate))
      certDetail = tr("%1, %2-bit key").arg(certDetail).arg(TlsUtility::getCertKeyLength(certificate));
    auto *certButtons = new QWidget();
    auto *certLayout = new QHBoxLayout(certButtons);
    certLayout->setContentsMargins(0, 0, 0, 0);
    auto *choose = new QPushButton(tr("Choose…"));
    connect(choose, &QPushButton::clicked, this, [this, certificate] {
      const auto file = QFileDialog::getSaveFileName(
          this, tr("Select a TLS certificate to use..."), certificate, QStringLiteral("Cert (*.pem)"), nullptr,
          QFileDialog::DontConfirmOverwrite
      );
      if (!file.isEmpty()) {
        Settings::setValue(Settings::Security::Certificate, file);
        scheduleRebuild();
      }
    });
    auto *regenerate = new QPushButton(tr("Regenerate"));
    connect(regenerate, &QPushButton::clicked, this, [this] {
      if (TlsUtility::generateCertificate()) {
        QMessageBox::information(
            this, tr("TLS Certificate Regenerated"), tr("TLS certificate regenerated successfully.")
        );
        scheduleRebuild();
      }
    });
    certLayout->addWidget(choose);
    certLayout->addWidget(regenerate);
    auto certRow = security->addRow(tr("Certificate"), certDetail, certButtons);
    certRow.detail->setToolTip(certificate);

    if (isServer) {
      security->addRow(
          tr("Only allow trusted computers"),
          tr("A computer can join only after you have confirmed its fingerprint here."),
          settingSwitch(Settings::Security::CheckPeers)
      );
    }

    auto *fingerprint = new QPushButton(tr("Show"));
    connect(fingerprint, &QPushButton::clicked, this, &SettingsPage::requestShowFingerprint);
    security->addRow(
        tr("This computer's fingerprint"), tr("Compare it with what the other computer shows when you connect."),
        fingerprint
    );
  }
  page->addWidget(security);
}

void SettingsPage::buildAdvanced(Page *page)
{
  if (deskflow::platform::isWindows() && !Settings::isPortableMode()) {
    page->addSection(tr("Background service"));
    auto *service = new FormCard();
    const bool useService =
        Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>() == Settings::ProcessMode::Service;
    auto *serviceSwitch = new ToggleSwitch();
    serviceSwitch->setChecked(useService);
    connect(serviceSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
      Settings::setValue(
          Settings::Core::ProcessMode,
          QVariant::fromValue(on ? Settings::ProcessMode::Service : Settings::ProcessMode::Desktop)
      );
      scheduleRebuild();
    });
    service->addRow(
        tr("Use the background service"), tr("Keeps sharing running when you switch users or lock the screen."),
        serviceSwitch
    );
    auto *elevate = settingSwitch(Settings::Daemon::Elevate);
    elevate->setEnabled(useService);
    service->addRow(
        tr("Run as system"), tr("Also works at the login screen and on administrator (UAC) prompts."), elevate
    );
    page->addWidget(service);
  }

  page->addSection(tr("Commands"));
  auto *commands = new FormCard();
  const auto addCommand = [this, commands](const QString &enableKey, const QString &commandKey, const QString &title) {
    auto *toggle = settingSwitch(enableKey);
    commands->addRow(title, QString(), toggle);
    auto *line = settingLine(commandKey, tr("Command to run"));
    line->setEnabled(toggle->isChecked());
    connect(toggle, &ToggleSwitch::toggled, line, &QLineEdit::setEnabled);
    auto *holder = new QWidget();
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 8, 0, 10);
    layout->addWidget(line);
    commands->addWidget(holder);
  };
  addCommand(
      Settings::Core::EnableEnterCommand, Settings::Core::ComputerEnterCommand,
      tr("Run a command when the cursor arrives on this computer")
  );
  addCommand(
      Settings::Core::EnableExitCommand, Settings::Core::ComputerExitCommand,
      tr("Run a command when the cursor leaves this computer")
  );
  page->addWidget(commands);

  page->addSection(tr("Logging"));
  auto *logging = new FormCard();
  auto *level = new QComboBox();
  const auto options = LogLevel::logLevelOptions();
  for (int i = 0; i < options.size(); ++i)
    level->addItem(LogLevel::toString(i), options.at(i));
  level->setCurrentIndex(level->findData(Settings::logLevelText(), Qt::UserRole, Qt::MatchFixedString));
  auto levelRow = logging->addRow(tr("Detail"), QString(), level);
  const auto updateLevelHint = [levelRow, level] {
    const bool verbose = level->currentIndex() > static_cast<int>(LogLevel::Level::Info);
    levelRow.detail->setText(verbose ? tr("More detail than Info can slow things down.") : QString());
    levelRow.detail->setVisible(verbose);
  };
  updateLevelHint();
  connect(level, &QComboBox::currentIndexChanged, this, [level, updateLevelHint] {
    Settings::setValue(Settings::Log::Level, level->currentData());
    updateLevelHint();
  });

  const auto logFile = Settings::value(Settings::Log::File).toString();
  auto *toFile = settingSwitch(Settings::Log::ToFile);
  auto fileRow = logging->addRow(tr("Save the log to a file"), elidedPath(logFile), toFile);
  fileRow.detail->setToolTip(logFile);
  auto *choose = new QPushButton(tr("Choose…"));
  choose->setEnabled(toFile->isChecked());
  connect(toFile, &ToggleSwitch::toggled, choose, &QPushButton::setEnabled);
  connect(choose, &QPushButton::clicked, this, [this, logFile] {
    const auto file =
        QFileDialog::getSaveFileName(this, tr("Save log file to..."), logFile, QStringLiteral("Logs (*.log *.txt)"));
    if (!file.isEmpty()) {
      Settings::setValue(Settings::Log::File, file);
      scheduleRebuild();
    }
  });
  logging->addRow(tr("Log file"), QString(), choose);
  logging->addRow(tr("Include GUI debug messages"), QString(), settingSwitch(Settings::Log::GuiDebug));
  page->addWidget(logging);

  page->addSection(tr("Reset"));
  auto *reset = new FormCard();
  auto *removeAll = new QPushButton(tr("Remove All Settings…"));
  removeAll->setProperty("destructive", true);
  connect(removeAll, &QPushButton::clicked, this, &SettingsPage::resetAllSettings);
  reset->addRow(
      tr("Start over"), tr("Removes every setting, including the screen layout, and restarts Deskflow."), removeAll
  );
  page->addWidget(reset);
}

void SettingsPage::resetAllSettings()
{
  const auto result = QMessageBox::question(
      this, tr("%1 Clear Settings").arg(kAppName),
      tr("<p>Are you sure you want to clear all settings and restart %1?</p> <p>This action cannot be undone.</p>")
          .arg(kAppName)
  );
  if (result == QMessageBox::Yes)
    Q_EMIT requestRemoveAllSettings();
}

} // namespace deskflow::gui::pages
