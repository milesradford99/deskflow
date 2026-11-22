/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "deskflow/App.h"
#include "net/NetworkAddress.h"

#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace deskflow {
class Computer;
class ClientArgs;
} // namespace deskflow
namespace deskflow::client {
class ClientHub;
} // namespace deskflow::client

class Event;
class EventQueueTimer;
class Client;
class Thread;
class ISocketFactory;

//! The core in client mode
/*!
Connects this computer to one server, or to several at once (Settings::Client::ExtraServers).
Each server is a session with its own connection, retries and failover list; they share
one computer through a ClientHub, which decides which server is in control.
*/
class ClientApp : public App
{
public:
  explicit ClientApp(IEventQueue *events, const QString &processName = QString());
  ~ClientApp() override;

  //
  // IApp overrides
  //

  void parseArgs() override;
  const char *daemonName() const override;
  void loadConfig() override
  {
    // do nothing
  }
  bool loadConfig(const QString &) override
  {
    return false;
  }
  int start() override;
  int runInner(StartupFunc startup) override;
  deskflow::Computer *createComputer() override;
  int mainLoop() override;
  void startNode() override;

  //
  // Regular functions
  //
  bool startClient();
  void stopClient();

  //
  // Static functions
  //

  static ClientApp &instance()
  {
    return (ClientApp &)App::instance();
  }

private:
  //! One server (with its failover addresses) and the connection to it
  struct Session
  {
    QString label; //!< as configured, for logs and the GUI
    QList<NetworkAddress> addresses;
    Client *client = nullptr;
    qsizetype currentServerIndex = 0;
    size_t lastAddressIndex = 0;
    uint retryCount = 0;
    deskflow::core::ConnectionState state = deskflow::core::ConnectionState::Disconnected;
    EventQueueTimer *restartTimer = nullptr;
    bool stopped = false; //!< refused for good; never retried
  };

  static double retryTime(uint retryCount);
  bool addSession(const QString &hosts);
  ISocketFactory *getSocketFactory() const;
  deskflow::Computer *openClientComputer();
  void closeClientComputer(deskflow::Computer *computer);
  Client *openClient(Session &session);
  void closeClient(Session &session);
  void connectSession(Session &session);
  void scheduleRestart(Session &session, double retryTime);
  void scheduleComputerRestart();
  void cancelRestart(Session &session);
  void tryNextServer(Session &session);

  void handleClientConnected(Session &session);
  void handleClientFailed(Session &session, const Event &e);
  void handleClientRefused(Session &session, const Event &e);
  void handleClientDisconnected(Session &session);

  void setState(Session &session, deskflow::core::ConnectionState state);
  //! Tell the GUI how every server is doing, and the overall state.
  void publishStates();

  std::vector<std::unique_ptr<Session>> m_sessions;
  deskflow::Computer *m_clientComputer = nullptr;
  std::unique_ptr<deskflow::client::ClientHub> m_hub;
  deskflow::core::ConnectionState m_publishedState = deskflow::core::ConnectionState::Disconnected;
  bool m_publishedOnce = false;
};
