/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Log.h"
#include "deskflow/FileTransferProtocol.h"
#include "server/Server.h"

namespace protocol = deskflow::fileTransferProtocol;

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events)
{
  // do nothing
}

bool ClientProxy1_9::sendFileTransferMessage(const deskflow::FileTransferMessage &message)
{
  protocol::write(getStream(), message);
  return true;
}

void ClientProxy1_9::sendFilePeers(const QStringList &names)
{
  protocol::writePeerList(getStream(), names);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (!protocol::isMessage(code)) {
    return ClientProxy1_8::parseMessage(code);
  }

  deskflow::FileTransferMessage message;
  if (!protocol::read(getStream(), code, message)) {
    LOG_ERR("malformed file transfer message from \"%s\"", getName().c_str());
    return false;
  }

  getServer()->handleFileTransferMessage(this, message);
  return true;
}
