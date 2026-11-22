/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/FileTransferProtocol.h"

namespace protocol = deskflow::fileTransferProtocol;

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events),
      m_client(client)
{
  // do nothing
}

bool ServerProxy1_9::sendFileTransferMessage(const deskflow::FileTransferMessage &message)
{
  protocol::write(getStream(), message);
  return true;
}

bool ServerProxy1_9::parseFileMessage(const uint8_t *code)
{
  if (protocol::isMessage(code)) {
    deskflow::FileTransferMessage message;
    if (protocol::read(getStream(), code, message)) {
      m_client->handleFileTransferMessage(message);
    } else {
      LOG_ERR("malformed file transfer message from server");
    }
    return true;
  }

  if (protocol::isPeerList(code)) {
    if (QStringList names; protocol::readPeerList(getStream(), names)) {
      m_client->setFilePeers(names);
    }
    return true;
  }

  return false;
}

// The server announces the computers as soon as it adopts this client,
// which can be before the handshake is over on our side.
ServerProxy::ConnectionResult ServerProxy1_9::parseHandshakeMessage(const uint8_t *code)
{
  return parseFileMessage(code) ? ConnectionResult::Okay : ServerProxy1_8::parseHandshakeMessage(code);
}

ServerProxy::ConnectionResult ServerProxy1_9::parseMessage(const uint8_t *code)
{
  return parseFileMessage(code) ? ConnectionResult::Okay : ServerProxy1_8::parseMessage(code);
}
