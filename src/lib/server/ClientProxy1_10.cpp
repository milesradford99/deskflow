/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_10.h"

#include "deskflow/ProtocolTypes.h"
#include "server/Server.h"

#include <cstring>

bool ClientProxy1_10::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCControlDenied, 4) == 0) {
    getServer()->handleControlDenied(this);
    return true;
  }
  return ClientProxy1_9::parseMessage(code);
}
