/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_8.h"

//! Proxy for client implementing protocol version 1.9: file transfer
class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_9() override = default;

  bool sendFileTransferMessage(const deskflow::FileTransferMessage &message) override;
  void sendFilePeers(const QStringList &names) override;

protected:
  bool parseMessage(const uint8_t *code) override;
};
