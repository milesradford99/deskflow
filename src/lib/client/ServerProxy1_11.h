/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_10.h"

//! Proxy for server implementing protocol version 1.11: remote view
class ServerProxy1_11 : public ServerProxy1_10
{
public:
  using ServerProxy1_10::ServerProxy1_10;
  ~ServerProxy1_11() override = default;

  bool sendRemoteViewFrame(const std::string &piece) override;
  void sendRemoteViewInfo(const std::string &json) override;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;
};
