/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_11.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

bool ServerProxy1_11::sendRemoteViewFrame(const std::string &piece)
{
  ProtocolUtil::writef(getStream(), kMsgDRemoteFrame, &piece);
  return true;
}

void ServerProxy1_11::sendRemoteViewInfo(const std::string &json)
{
  ProtocolUtil::writef(getStream(), kMsgDRemoteInfo, &json);
}

ServerProxy::ConnectionResult ServerProxy1_11::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDRemoteSettings, 4) == 0) {
    std::string json;
    if (ProtocolUtil::readf(getStream(), kMsgDRemoteSettings + 4, &json)) {
      getClient()->handleRemoteViewSettings(json);
    } else {
      LOG_ERR("malformed remote view settings from server");
    }
    return ConnectionResult::Okay;
  }
  if (memcmp(code, kMsgDRemoteAck, 4) == 0) {
    uint32_t sequence = 0;
    if (ProtocolUtil::readf(getStream(), kMsgDRemoteAck + 4, &sequence)) {
      getClient()->handleRemoteViewAck(sequence);
    }
    return ConnectionResult::Okay;
  }
  return ServerProxy1_10::parseMessage(code);
}
