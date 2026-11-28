/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_10.h"

//! Proxy for client implementing protocol version 1.11: remote view
class ClientProxy1_11 : public ClientProxy1_10
{
public:
  using ClientProxy1_10::ClientProxy1_10;
  ~ClientProxy1_11() override = default;

  bool sendRemoteViewSettings(const std::string &json) override;
  void sendRemoteViewAck(uint32_t sequence) override;

protected:
  bool parseMessage(const uint8_t *code) override;
};
