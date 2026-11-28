/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <functional>

void requestOSXNotificationPermission();
bool isOSXDevelopmentBuild();
bool showOSXNotification(const QString &title, const QString &body);
bool isOSXInterfaceStyleDark();
void forceAppActive();
void macOSNativeHide();
class QWidget;
/// Bring an overlay window to the front without activating the app (QWidget::raise() activates
/// the whole app on macOS, which also brings the main window forward).
void macOSOrderFrontWithoutActivating(QWidget *window);
void installQuitHandler(std::function<bool()> shouldQuit);

/// NSPasteboard change count, increments every time the general pasteboard changes.
long macPasteboardChangeCount();
/// True when the general pasteboard carries a concealed/transient marker (password managers).
bool macPasteboardIsConcealed();
/// True when this app may record the screen (needed for screenshots that show other apps' windows).
bool macScreenCaptureAllowed();
/// Ask for screen recording access; macOS shows its prompt only the first time.
void macRequestScreenCaptureAccess();
