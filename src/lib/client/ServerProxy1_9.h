/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"

//! Proxy for server implementing protocol version 1.9: file transfer
class ServerProxy1_9 : public ServerProxy1_8
{
public:
  ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxy1_9() override = default;

  bool sendFileTransferMessage(const deskflow::FileTransferMessage &message) override;

protected:
  ConnectionResult parseHandshakeMessage(const uint8_t *code) override;
  ConnectionResult parseMessage(const uint8_t *code) override;

private:
  //! Handles a file transfer family message; returns false if \c code isn't one.
  bool parseFileMessage(const uint8_t *code);

  Client *m_client;
};
