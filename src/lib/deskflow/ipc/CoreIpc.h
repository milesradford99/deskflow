/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "deskflow/KeyTypes.h"

#include <QString>
#include <QStringList>

//! Broadcast to the connected GUIs. Messages sent while none is connected are replayed
//! to the first one that does, unless \c queueIfNoClients is false.
void ipcSendToClient(const QString &command, const QString &args = "", bool queueIfNoClients = true);
void ipcSendConnectionState(deskflow::core::ConnectionState state);

/**
 * @brief Send a live activity event (screen switch, clipboard sync, ...) for the GUI overlay.
 *
 * Sent as `activity=<type>,<field>,...` with each part percent-encoded.
 * Unlike ipcSendToClient, the event is dropped when no GUI is connected; replaying stale
 * activity later would be misleading. Safe to call from any thread.
 */
void ipcSendActivity(const QString &type, const QStringList &fields = {});

/**
 * @brief Send a key press to the GUI overlay, only if the GUI asked for key presses.
 *
 * Key presses are never queued and never written to the log. Safe to call from any thread.
 */
void ipcSendKeyPress(KeyID id, KeyModifierMask mask);

//! Whether a connected GUI currently wants key presses (toggled by the `overlayKeys` ipc command).
bool ipcKeyOverlayEnabled();
void ipcSetKeyOverlayEnabled(bool enabled);
