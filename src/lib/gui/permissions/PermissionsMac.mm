/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Permissions.h"

#include <QDebug>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Security/Security.h>

namespace deskflow::gui::permissions::platform {

namespace {

//! Service names understood by tccutil.
QString service(Kind kind)
{
  switch (kind) {
  case Kind::Accessibility:
    return QStringLiteral("Accessibility");
  case Kind::InputMonitoring:
    return QStringLiteral("ListenEvent");
  case Kind::ScreenRecording:
    return QStringLiteral("ScreenCapture");
  }
  return {};
}

QString bundleId()
{
  CFStringRef id = CFBundleGetIdentifier(CFBundleGetMainBundle());
  return id ? QString::fromCFString(id) : QString();
}

bool tccReset(const QString &name)
{
  const auto id = bundleId();
  if (id.isEmpty())
    return false;
  qInfo().noquote() << "clearing permission entry" << name << "for" << id;
  return QProcess::execute(QStringLiteral("/usr/bin/tccutil"), {QStringLiteral("reset"), name, id}) == 0;
}

SecStaticCodeRef copySelf()
{
  SecCodeRef code = nullptr;
  if (SecCodeCopySelf(kSecCSDefaultFlags, &code) != errSecSuccess || code == nullptr)
    return nullptr;
  SecStaticCodeRef staticCode = nullptr;
  SecCodeCopyStaticCode(code, kSecCSDefaultFlags, &staticCode);
  CFRelease(code);
  return staticCode;
}

//! The leaf certificate's summary, empty for ad-hoc signatures.
QString certificateName()
{
  SecStaticCodeRef code = copySelf();
  if (code == nullptr)
    return {};
  QString name;
  CFDictionaryRef info = nullptr;
  if (SecCodeCopySigningInformation(code, kSecCSSigningInformation, &info) == errSecSuccess && info != nullptr) {
    auto certificates = static_cast<CFArrayRef>(CFDictionaryGetValue(info, kSecCodeInfoCertificates));
    if (certificates != nullptr && CFArrayGetCount(certificates) > 0) {
      auto leaf = static_cast<SecCertificateRef>(const_cast<void *>(CFArrayGetValueAtIndex(certificates, 0)));
      if (CFStringRef summary = SecCertificateCopySubjectSummary(leaf); summary != nullptr) {
        name = QString::fromCFString(summary);
        CFRelease(summary);
      }
    }
    CFRelease(info);
  }
  CFRelease(code);
  return name;
}

} // namespace

bool supported()
{
  return true;
}

bool granted(Kind kind)
{
  switch (kind) {
  case Kind::Accessibility:
    return AXIsProcessTrusted();
  case Kind::InputMonitoring:
    return CGPreflightListenEventAccess();
  case Kind::ScreenRecording:
    return CGPreflightScreenCaptureAccess();
  }
  return false;
}

void prompt(Kind kind)
{
  switch (kind) {
  case Kind::Accessibility: {
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options =
        CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    break;
  }
  case Kind::InputMonitoring:
    CGRequestListenEventAccess();
    break;
  case Kind::ScreenRecording:
    CGRequestScreenCaptureAccess();
    break;
  }
}

bool reset(Kind kind)
{
  return tccReset(service(kind));
}

bool resetAll()
{
  return tccReset(QStringLiteral("All"));
}

void openSettings(Kind kind)
{
  QString anchor;
  switch (kind) {
  case Kind::Accessibility:
    anchor = QStringLiteral("Privacy_Accessibility");
    break;
  case Kind::InputMonitoring:
    anchor = QStringLiteral("Privacy_ListenEvent");
    break;
  case Kind::ScreenRecording:
    anchor = QStringLiteral("Privacy_ScreenCapture");
    break;
  }
  QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?") + anchor));
}

QString signature()
{
  SecStaticCodeRef code = copySelf();
  if (code == nullptr)
    return {};
  QString text;
  SecRequirementRef requirement = nullptr;
  if (SecCodeCopyDesignatedRequirement(code, kSecCSDefaultFlags, &requirement) == errSecSuccess &&
      requirement != nullptr) {
    CFStringRef string = nullptr;
    if (SecRequirementCopyString(requirement, kSecCSDefaultFlags, &string) == errSecSuccess && string != nullptr) {
      text = QString::fromCFString(string);
      CFRelease(string);
    }
    CFRelease(requirement);
  }
  CFRelease(code);
  return text;
}

bool stableSignature()
{
  return !certificateName().isEmpty();
}

QString signer()
{
  return certificateName();
}

bool scheduleRelaunch(qint64 pid)
{
  NSString *path = [[NSBundle mainBundle] bundlePath];
  if (path == nil || ![path hasSuffix:@".app"])
    return false;
  // Wait for this process to go, so the new one gets the single-instance lock and fresh permissions.
  const auto script =
      QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; sleep 0.3; /usr/bin/open -n \"$0\"");
  return QProcess::startDetached(
      QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script, QString::fromNSString(path), QString::number(pid)}
  );
}

} // namespace deskflow::gui::permissions::platform
