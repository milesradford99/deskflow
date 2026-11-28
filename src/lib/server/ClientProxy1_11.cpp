/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_11.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/Server.h"

#include <cstring>

bool ClientProxy1_11::sendRemoteViewSettings(const std::string &json)
{
  ProtocolUtil::writef(getStream(), kMsgDRemoteSettings, &json);
  return true;
}

void ClientProxy1_11::sendRemoteViewAck(uint32_t sequence)
{
  ProtocolUtil::writef(getStream(), kMsgDRemoteAck, sequence);
}

bool ClientProxy1_11::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDRemoteFrame, 4) == 0) {
    std::string piece;
    if (!ProtocolUtil::readf(getStream(), kMsgDRemoteFrame + 4, &piece)) {
      LOG_ERR("malformed remote view frame from \"%s\"", getName().c_str());
      return false;
    }
    getServer()->handleRemoteViewFrame(this, piece);
    return true;
  }
  if (memcmp(code, kMsgDRemoteInfo, 4) == 0) {
    std::string info;
    if (!ProtocolUtil::readf(getStream(), kMsgDRemoteInfo + 4, &info)) {
      LOG_ERR("malformed remote view info from \"%s\"", getName().c_str());
      return false;
    }
    getServer()->handleRemoteViewInfo(this, info);
    return true;
  }
  return ClientProxy1_10::parseMessage(code);
}
