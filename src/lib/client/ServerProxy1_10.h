/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_9.h"

//! Proxy for server implementing protocol version 1.10: clients with several servers
class ServerProxy1_10 : public ServerProxy1_9
{
public:
  using ServerProxy1_9::ServerProxy1_9;
  ~ServerProxy1_10() override = default;

  void sendControlDenied() override;
};
