/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClientApp.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "client/Client.h"
#include "client/ClientHub.h"
#include "common/ExitCodes.h"
#include "common/IpcPayload.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "deskflow/Computer.h"
#include "deskflow/DisplayException.h"
#include "deskflow/ipc/CoreIpc.h"
#include "net/NetworkAddress.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocketFactory.h"

#if defined(Q_OS_WIN)
#include "platform/MSWindowsComputer.h"
#endif

#include <QFileInfo> // Must include before XWindowsComputer to avoid conflicts with xlib.h

#if WINAPI_XWINDOWS
#include "platform/XWindowsComputer.h"
#endif

#if WINAPI_LIBEI
#include "platform/EiComputer.h"
#endif

#if defined(Q_OS_MACOS)
#include "platform/OSXComputer.h"
#endif

#include <QJsonArray>
#include <QJsonObject>

#include <memory>

ClientApp::ClientApp(IEventQueue *events, const QString &processName) : App(events, processName)
{
  // do nothing
}

ClientApp::~ClientApp() = default;

void ClientApp::parseArgs()
{
  // the main server (a comma-separated list is tried in turn), then any servers to
  // stay connected to at the same time, each with the same syntax
  QStringList servers;
  if (const auto main = Settings::value(Settings::Client::RemoteHost).toString(); !main.trimmed().isEmpty())
    servers.append(main);
  for (const auto &extra : Settings::value(Settings::Client::ExtraServers).toStringList()) {
    if (!extra.trimmed().isEmpty() && !servers.contains(extra))
      servers.append(extra);
  }

  for (const auto &hosts : servers) {
    if (!addSession(hosts)) {
      LOG_CRIT(
          "%s: no valid server addresses in \"%s\"" BYE, qPrintable(processName()), qPrintable(hosts),
          qPrintable(processName())
      );
      bye(s_exitFailed);
    }
  }

  if (m_sessions.size() > 1) {
    LOG_INFO("connecting to %zu servers at the same time", m_sessions.size());
  }
}

bool ClientApp::addSession(const QString &hosts)
{
  auto session = std::make_unique<Session>();
  session->label = hosts.trimmed();

  const int port = Settings::value(Settings::Core::Port).toInt();
  for (const QString &addr : hosts.split(',', Qt::SkipEmptyParts)) {
    const QString trimmedAddr = addr.trimmed();
    if (trimmedAddr.isEmpty()) {
      continue;
    }
    try {
      NetworkAddress netAddr(trimmedAddr.toStdString(), port);
      netAddr.resolve();
      session->addresses.append(netAddr);
      LOG_DEBUG("added server address: %s", qPrintable(trimmedAddr));
    } catch (SocketAddressException &e) {
      // allow an address that we can't look up if we're restartable.
      // we'll try to resolve the address each time we connect to the
      // server.  a bad port will never get better.
      if (e.getError() == SocketAddressException::SocketError::BadPort) {
        LOG_CRIT("%s: %s" BYE, qPrintable(processName()), e.what(), qPrintable(processName()));
        bye(s_exitFailed);
      } else {
        // Still add it - we'll try to resolve later
        NetworkAddress netAddr(trimmedAddr.toStdString(), port);
        session->addresses.append(netAddr);
        LOG_WARN("could not resolve address '%s': %s (will retry later)", qPrintable(trimmedAddr), e.what());
      }
    }
  }

  if (session->addresses.isEmpty())
    return false;
  LOG_INFO(
      "configured %zu address(es) for server \"%s\"", static_cast<size_t>(session->addresses.size()),
      qPrintable(session->label)
  );
  m_sessions.push_back(std::move(session));
  return true;
}

const char *ClientApp::daemonName() const
{
  if (deskflow::platform::isWindows())
    return "Deskflow Client";
  return "deskflow-client";
}

deskflow::Computer *ClientApp::createComputer()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  const auto languageSync = Settings::value(Settings::Client::LanguageSync).toBool();
  LOG_INFO("keyboard language sync is %s", languageSync ? "enabled" : "disabled");
#endif

#if defined(Q_OS_WIN)
  return new deskflow::Computer(
      new MSWindowsComputer(false, Settings::value(Settings::Core::UseHooks).toBool(), getEvents(), languageSync),
      getEvents()
  );
#elif defined(Q_OS_MACOS)
  return new deskflow::Computer(new OSXComputer(getEvents(), false, languageSync), getEvents());
#else
  if (deskflow::platform::isWayland()) {
#if WINAPI_LIBEI
    LOG_INFO("detected wayland platform");
    return new deskflow::Computer(new deskflow::EiComputer(false, getEvents(), true), getEvents());
#else
    throw XNoEiSupport();
#endif
  }
#if WINAPI_XWINDOWS
  LOG_INFO("detected X11 platform");
  return new deskflow::Computer(
      new XWindowsComputer(qPrintable(Settings::value(Settings::Core::Display).toString()), false, getEvents()),
      getEvents()
  );
#endif
#endif // end os check
}

deskflow::Computer *ClientApp::openClientComputer()
{
  deskflow::Computer *computer = createComputer();
  getEvents()->addHandler(EventTypes::ComputerError, computer->getEventTarget(), [this](const auto &) {
    handleComputerError();
  });
  return computer;
}

void ClientApp::closeClientComputer(deskflow::Computer *computer)
{
  if (computer != nullptr) {
    getEvents()->removeHandler(EventTypes::ComputerError, computer->getEventTarget());
    delete computer;
  }
}

void ClientApp::scheduleRestart(Session &session, double retryTime)
{
  cancelRestart(session);
  LOG_DEBUG("retry \"%s\" in %.0f seconds", qPrintable(session.label), retryTime);
  if (m_publishedState != deskflow::core::ConnectionState::Connected) {
    ipcSendToClient("retryIn", QString::number(retryTime, 'f', 0));
  }

  // install a timer and handler to retry later
  auto *timer = getEvents()->newOneShotTimer(retryTime, nullptr);
  session.restartTimer = timer;
  getEvents()->addHandler(EventTypes::Timer, timer, [this, &session](const auto &) {
    cancelRestart(session);
    if (m_clientComputer == nullptr) {
      startClient();
    } else {
      connectSession(session);
    }
  });
}

void ClientApp::scheduleComputerRestart()
{
  // the computer couldn't be opened yet (e.g. no display): try everything again later
  ipcSendToClient("retryIn", QString::number(retryTime(0), 'f', 0));
  auto *timer = getEvents()->newOneShotTimer(retryTime(0), nullptr);
  getEvents()->addHandler(EventTypes::Timer, timer, [this, timer](const auto &) {
    getEvents()->removeHandler(EventTypes::Timer, timer);
    getEvents()->deleteTimer(timer);
    startClient();
  });
}

void ClientApp::cancelRestart(Session &session)
{
  if (session.restartTimer != nullptr) {
    getEvents()->removeHandler(EventTypes::Timer, session.restartTimer);
    getEvents()->deleteTimer(session.restartTimer);
    session.restartTimer = nullptr;
  }
}

void ClientApp::handleClientConnected(Session &session)
{
  LOG_DEBUG("connected to server \"%s\"", qPrintable(session.label));
  setState(session, deskflow::core::ConnectionState::Connected);

  // Reset server index on successful connection
  session.currentServerIndex = 0;
  session.lastAddressIndex = 0;
}

void ClientApp::handleClientFailed(Session &session, const Event &e)
{
  if ((++session.lastAddressIndex) < session.client->getLastResolvedAddressesCount()) {
    // Try next resolved address for current hostname
    std::unique_ptr<Client::FailInfo> info(static_cast<Client::FailInfo *>(e.getData()));
    LOG_WARN("failed to connect to server=%s, trying next resolved address", qPrintable(info->m_what));
    scheduleRestart(session, retryTime(session.retryCount));
  } else {
    // All resolved addresses exhausted, try next server in list
    session.lastAddressIndex = 0;
    tryNextServer(session);
    if (session.currentServerIndex == 0) {
      // We've cycled through all servers, treat as refused
      handleClientRefused(session, e);
    } else {
      std::unique_ptr<Client::FailInfo> info(static_cast<Client::FailInfo *>(e.getData()));
      LOG_WARN("failed to connect to server=%s, trying next server in list", qPrintable(info->m_what));
      scheduleRestart(session, retryTime(session.retryCount));
    }
  }
}

void ClientApp::handleClientRefused(Session &session, const Event &e)
{
  std::unique_ptr<Client::FailInfo> info(static_cast<Client::FailInfo *>(e.getData()));
  setState(session, deskflow::core::ConnectionState::Disconnected);
  if (info->m_retry) {
    LOG_WARN("failed to connect to server: %s", qPrintable(info->m_what));
    scheduleRestart(session, retryTime(session.retryCount));
    session.retryCount++;
    return;
  }

  LOG_ERR("failed to connect to server \"%s\": %s", qPrintable(session.label), qPrintable(info->m_what));
  session.stopped = true;

  // one server turning us down for good mustn't cut the others off
  for (const auto &other : m_sessions) {
    if (!other->stopped)
      return;
  }
  getEvents()->addEvent(Event(EventTypes::Quit));
}

void ClientApp::handleClientDisconnected(Session &session)
{
  session.retryCount = 0;
  LOG_DEBUG("disconnected from server \"%s\"", qPrintable(session.label));
  setState(session, deskflow::core::ConnectionState::Disconnected);
  scheduleRestart(session, retryTime(session.retryCount));
}

Client *ClientApp::openClient(Session &session)
{
  auto *client = new Client(
      getEvents(), Settings::value(Settings::Core::ComputerName).toString().toStdString(),
      session.addresses[session.currentServerIndex], getSocketFactory(), m_hub.get()
  );
  using enum EventTypes;
  auto *target = client->getEventTarget();
  auto *events = getEvents();
  events->addHandler(ClientConnected, target, [this, &session](const auto &) { handleClientConnected(session); });
  events->addHandler(ClientConnectionFailed, target, [this, &session](const auto &e) {
    handleClientFailed(session, e);
  });
  events->addHandler(ClientConnectionRefused, target, [this, &session](const auto &e) {
    handleClientRefused(session, e);
  });
  events->addHandler(ClientDisconnected, target, [this, &session](const auto &) { handleClientDisconnected(session); });
  return client;
}

void ClientApp::closeClient(Session &session)
{
  cancelRestart(session);
  if (session.client == nullptr) {
    return;
  }
  using enum EventTypes;
  auto *target = session.client->getEventTarget();
  getEvents()->removeHandler(ClientConnected, target);
  getEvents()->removeHandler(ClientConnectionFailed, target);
  getEvents()->removeHandler(ClientConnectionRefused, target);
  getEvents()->removeHandler(ClientDisconnected, target);
  delete session.client;
  session.client = nullptr;
}

void ClientApp::connectSession(Session &session)
{
  if (session.client == nullptr || session.stopped) {
    return;
  }
  session.client->setServerAddress(session.addresses[session.currentServerIndex]);
  if (!session.client->isConnected()) {
    setState(session, deskflow::core::ConnectionState::Connecting);
  }
  session.client->connect(session.lastAddressIndex);
}

bool ClientApp::startClient()
{
  if (m_sessions.empty()) {
    LOG_CRIT("failed to start client: no server address configured");
    return false;
  }

  if (m_clientComputer == nullptr) {
    deskflow::Computer *clientComputer = nullptr;
    try {
      clientComputer = openClientComputer();
      m_hub = std::make_unique<deskflow::client::ClientHub>(
          getEvents(), clientComputer, Settings::value(Settings::Core::ComputerName).toString().toStdString()
      );
      m_hub->setControlChangedHandler([this] { publishStates(); });
      m_clientComputer = clientComputer;
      for (const auto &session : m_sessions) {
        session->client = openClient(*session);
      }
      LOG_INFO("started client");
    } catch (X11DisplayUnavailableException &e) {
      LOG_WARN("secondary computer unavailable: %s", e.what());
      m_hub.reset();
      closeClientComputer(clientComputer);
      m_clientComputer = nullptr;
      scheduleComputerRestart();
      return true;
    } catch (DisplayOpenFailureException &e) {
      LOG_CRIT("failed to start client: %s", e.what());
      m_hub.reset();
      closeClientComputer(clientComputer);
      m_clientComputer = nullptr;
      return false;
    } catch (BaseException &e) {
      LOG_CRIT("failed to start client: %s", e.what());
      m_hub.reset();
      closeClientComputer(clientComputer);
      m_clientComputer = nullptr;
      return false;
    }
  }

  for (const auto &session : m_sessions) {
    connectSession(*session);
  }
  return true;
}

void ClientApp::stopClient()
{
  // clients first: they hand the computer back to the hub, which must outlive them
  for (const auto &session : m_sessions) {
    closeClient(*session);
    session->retryCount = 0;
  }
  m_hub.reset();
  closeClientComputer(m_clientComputer);
  m_clientComputer = nullptr;
}

void ClientApp::setState(Session &session, deskflow::core::ConnectionState state)
{
  session.state = state;
  publishStates();
}

void ClientApp::publishStates()
{
  using deskflow::core::ConnectionState;

  // overall: connected if any server is, else connecting if any is trying
  auto overall = ConnectionState::Disconnected;
  QJsonArray servers;
  const Client *controller = m_hub != nullptr ? m_hub->controller() : nullptr;
  for (const auto &session : m_sessions) {
    if (session->state == ConnectionState::Connected) {
      overall = ConnectionState::Connected;
    } else if (session->state == ConnectionState::Connecting && overall != ConnectionState::Connected) {
      overall = ConnectionState::Connecting;
    }
    QJsonObject server;
    server["host"] = session->label;
    server["state"] = static_cast<int>(session->state);
    server["control"] = controller != nullptr && controller == session->client;
    servers.append(server);
  }

  if (!m_publishedOnce || overall != m_publishedState) {
    m_publishedOnce = true;
    m_publishedState = overall;
    ipcSendConnectionState(overall);
  }

  QJsonObject payload;
  payload["servers"] = servers;
  ipcSendToClient(deskflow::ipc::kServers, deskflow::ipc::encodePayload(payload));
}

int ClientApp::mainLoop()
{
  // create socket multiplexer.  this must happen after daemonization
  // on unix because threads evaporate across a fork().
  setSocketMultiplexer(std::make_unique<SocketMultiplexer>());

  // start client, etc
  startNode();

  // run event loop.  if startClient() failed we're supposed to retry
  // later.  the timer installed by startClient() will take care of
  // that.
  int exitCode = getEvents()->loop();

  // close down
  LOG_DEBUG("stopping client");
  stopClient();
  LOG_INFO("stopped client");

  return exitCode;
}

int ClientApp::start()
{
  initApp();
  return mainLoop();
}

int ClientApp::runInner(StartupFunc startup)
{
  int result;
  try {
    // run
    result = startup();
  } catch (...) {
    throw;
  }

  return result;
}

void ClientApp::tryNextServer(Session &session)
{
  if (session.addresses.size() > 1) {
    session.currentServerIndex = (session.currentServerIndex + 1) % session.addresses.size();
    LOG_DEBUG(
        "switching to server %zu of %zu", static_cast<size_t>(session.currentServerIndex + 1),
        static_cast<size_t>(session.addresses.size())
    );
  }
}

void ClientApp::startNode()
{
  // start the client.  if this return false then we've failed and
  // we shouldn't retry.
  LOG_VERBOSE("starting client");
  if (!startClient()) {
    bye(s_exitFailed);
  }
}

ISocketFactory *ClientApp::getSocketFactory() const
{
  return new TCPSocketFactory(getEvents(), getSocketMultiplexer());
}

double ClientApp::retryTime(uint retryCount)
{
  if (!Settings::value(Settings::Client::DynamicConnectionRetry).toBool() || retryCount < 300) // 5 minutes
    return 1;
  if (retryCount < 360) // 5 minutes
    return 5;
  if (retryCount < 390) // 5 minutes
    return 10;
  if (retryCount < 410) // 10 minutes
    return 30;
  if (retryCount < 420) // 10 minutes
    return 60;
  if (retryCount < 430) // 20 minutes
    return 120;
  return 300;
}
