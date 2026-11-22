/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_10.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

void ServerProxy1_10::sendControlDenied()
{
  LOG_DEBUG("telling the server another server is in control");
  ProtocolUtil::writef(getStream(), kMsgCControlDenied);
}
