/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_9.h"

//! Proxy for client implementing protocol version 1.10: clients with several servers
class ClientProxy1_10 : public ClientProxy1_9
{
public:
  using ClientProxy1_9::ClientProxy1_9;
  ~ClientProxy1_10() override = default;

protected:
  bool parseMessage(const uint8_t *code) override;
};
