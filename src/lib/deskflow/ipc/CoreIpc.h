/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QString>

//! Broadcast to the connected GUIs. Messages sent while none is connected are replayed
//! to the first one that does, unless \c queueIfNoClients is false.
void ipcSendToClient(const QString &command, const QString &args = "", bool queueIfNoClients = true);
void ipcSendConnectionState(deskflow::core::ConnectionState state);
