/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "common/I18N.h"
#include "common/PlatformInfo.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "gui/Diagnostic.h"
#include "gui/MainWindow.h"
#include "gui/Messages.h"
#include "gui/StyleUtils.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSharedMemory>
#include <QTimer>
#include <QUrl>

#if defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <cstdlib>
#endif

#if defined(Q_OS_UNIX) && defined(QT_DEBUG)
#include <QLoggingCategory>
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
#include "platform/XDGPortalRegistry.h"
#endif

using namespace deskflow::gui;

#if defined(Q_OS_MACOS)
bool checkMacAssistiveDevices();
#endif

const static auto kHeader = QStringLiteral("%1: %2\n").arg(kAppName, kDisplayVersion);

int main(int argc, char *argv[])
{
#if defined(Q_OS_UNIX) && defined(QT_DEBUG)
  // Fixes Fedora bug where qDebug() messages aren't printed.
  QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true\nqt.*=false"));
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
  if (QT_VERSION < QT_VERSION_CHECK(6, 10, 0))
    deskflow::platform::setAppId();
#endif

  QCoreApplication::setApplicationName(kAppName);
  QCoreApplication::setOrganizationName(kAppName);
  QCoreApplication::setApplicationVersion(kVersion);
  QCoreApplication::setOrganizationDomain(kOrgDomain); // used in prefix, can't be a url
  QGuiApplication::setDesktopFileName(kRevFqdnName);

  QApplication app(argc, argv);

  // Ensure the I18N object is made before strings
  QTextStream(stdout) << "initial language: " << I18N::currentLanguage() << '\n';

  // Add Command Line Options
  auto helpOption = QCommandLineOption({"h", "help"}, "Display Help on the command line");
  auto versionOption = QCommandLineOption({"v", "version"}, "Display version information");
  auto resetOption = QCommandLineOption("reset", "Reset all settings");

  QCommandLineParser parser;
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.addOption(helpOption);
  parser.addOption(versionOption);
  parser.addOption(resetOption);
  parser.parse(QCoreApplication::arguments());

  if (!parser.errorText().isEmpty()) {
    qCritical().noquote() << parser.errorText() << "\nUse --help for more information.";
    return s_exitArgs;
  }

  if (parser.isSet(helpOption)) {
    QTextStream(stdout) << kHeader << QStringLiteral("  %1\n\n").arg(kAppDescription)
                        << parser.helpText().replace(QApplication::applicationFilePath(), kAppId);
    return s_exitSuccess;
  }

  if (parser.isSet(versionOption)) {
    QTextStream(stdout) << kHeader << kCopyright << Qt::endl;
    return s_exitSuccess;
  }

  const auto shmId = QStringLiteral("%1-gui").arg(kAppId);
  // Create a shared memory segment with a unique key
  // This is to prevent a new instance from running if one is already running
  QSharedMemory sharedMemory(shmId);

  // Attempt to attach first and detach in order to clean up stale shm chunks
  // This can happen if the previous instance was killed or crashed
  if (sharedMemory.attach())
    sharedMemory.detach();

  // If we can create 1 byte of SHM we are the only instance
  if (!sharedMemory.create(1)) {
    // Ping the running instance to have it show itself
    QLocalSocket socket;
    socket.connectToServer(shmId, QLocalSocket::ReadOnly);
    if (!socket.waitForConnected()) {
      // If we can't connect to the other instance tell the user its running.
      // This should never happen but just incase we should show something
      QMessageBox::information(nullptr, kAppName, QObject::tr("%1 is already running").arg(kAppName));
    }
    socket.disconnectFromServer();
    return s_exitDuplicate;
  }

  // one look on every platform; also picks the icon set that matches the colour scheme
  deskflow::gui::theme::apply(app);

  qInstallMessageHandler(deskflow::gui::messages::messageHandler);
  qInfo("%s v%s", kAppName, kDisplayVersion);

#if defined(Q_OS_MACOS)

  if (app.applicationDirPath().startsWith("/Volumes/")) {
    QString msgBody = QStringLiteral(
        "Please drag %1 to the Applications folder, "
        "and open it from there."
    );
    QMessageBox::information(nullptr, kAppName, msgBody.arg(kAppName));
    return 1;
  }

  if (!checkMacAssistiveDevices()) {
    return 1;
  }
#endif

  // --no-reset
  if (parser.isSet(resetOption)) {
    diagnostic::clearSettings(false);
  }

  MainWindow mainWindow;
  mainWindow.open();

  return QApplication::exec();
}

#if defined(Q_OS_MACOS)
bool checkMacAssistiveDevices()
{
  // Applications are trusted individually through the Accessibility list in
  // Privacy & Security; the core needs it to read and send input.
  if (AXIsProcessTrusted()) {
    return true;
  }

  // macOS ties the entry to the app's code signature. Each new ad-hoc signed build
  // leaves the old entry switched on but no longer matching, and the switch then
  // does nothing. Clearing our own entry makes the prompt add a fresh one.
  if (CFStringRef bundleId = CFBundleGetIdentifier(CFBundleGetMainBundle()); bundleId != nullptr) {
    const auto id = QString::fromCFString(bundleId);
    qInfo().noquote() << "not trusted for accessibility, clearing any stale entry for" << id;
    const QStringList args = {QStringLiteral("reset"), QStringLiteral("Accessibility"), id};
    if (QProcess::execute(QStringLiteral("/usr/bin/tccutil"), args) != 0) {
      qWarning("unable to clear the accessibility entry");
    }
  }

  const void *keys[] = {kAXTrustedCheckOptionPrompt};
  const void *trueValue[] = {kCFBooleanTrue};
  CFDictionaryRef options = CFDictionaryCreate(nullptr, keys, trueValue, 1, nullptr, nullptr);
  const bool trusted = AXIsProcessTrustedWithOptions(options);
  CFRelease(options);
  if (trusted) {
    return true;
  }

  // Wait for the permission instead of quitting, so the first launch carries on by
  // itself once the switch is turned on.
  QMessageBox box;
  box.setIcon(QMessageBox::Information);
  box.setWindowTitle(kAppName);
  box.setText(QObject::tr("%1 needs Accessibility permission to share your keyboard and mouse.").arg(kAppName));
  box.setInformativeText(
      QObject::tr(
          "In System Settings, open Privacy & Security > Accessibility and switch on %1. "
          "This window closes by itself once the permission is granted.\n\n"
          "If %1 is already switched on, remove it with the minus button and open %1 again."
      )
          .arg(kAppName)
  );
  const auto openSettings = box.addButton(QObject::tr("Open System Settings"), QMessageBox::ActionRole);
  const auto quit = box.addButton(QObject::tr("Quit"), QMessageBox::RejectRole);

  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &box, [&box] {
    if (AXIsProcessTrusted()) {
      box.done(QMessageBox::Ok);
    }
  });
  poll.start(500);

  // any button closes the box, so show it again until the permission arrives or the user quits
  while (!AXIsProcessTrusted()) {
    box.exec();
    if (AXIsProcessTrusted()) {
      break;
    }
    if (box.clickedButton() == quit) {
      return false;
    }
    if (box.clickedButton() == openSettings) {
      QDesktopServices::openUrl(
          QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"))
      );
    }
  }
  return true;
}
#endif
