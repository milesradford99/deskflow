/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClientHub.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "client/Client.h"
#include "common/Settings.h"
#include "deskflow/ClipboardFileSync.h"
#include "deskflow/Computer.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/FileTransferIpc.h"

namespace deskflow::client {

namespace {
// Links are how the file transfer manager tells servers apart; no computer can have this name.
const auto kLinkPrefix = std::string("\x01server-");
} // namespace

ClientHub::ClientHub(IEventQueue *events, deskflow::Computer *computer, std::string localName)
    : m_events(events),
      m_computer(computer),
      m_localName(std::move(localName)),
      m_arbiter(*this, [this] { return m_clock.elapsed(); }),
      m_shareClipboard(Settings::value(Settings::Client::ShareClipboardBetweenServers).toBool())
{
  m_clock.start();
  m_arbiter.setTakeoverDelay(Settings::value(Settings::Client::TakeoverDelay).toInt());

  // events the computer raises, handed to every client
  auto *target = m_computer->getEventTarget();
  m_events->addHandler(EventTypes::ComputerSuspend, target, [this](const auto &) {
    for (const auto &[id, session] : m_sessions)
      session.client->handleSuspend();
  });
  m_events->addHandler(EventTypes::ComputerResume, target, [this](const auto &) {
    for (const auto &[id, session] : m_sessions)
      session.client->handleResume();
  });
  m_events->addHandler(EventTypes::ComputerShapeChanged, target, [this](const auto &) {
    forEachReady([](Client *client) { client->handleShapeChanged(); });
  });
  m_events->addHandler(EventTypes::ClipboardGrabbed, target, [this](const auto &e) { handleClipboardGrabbed(e); });

  // one file transfer manager for every server: it answers on the link a message came in
  // on, and new transfers go to whichever server knows the target computer
  m_fileTransfer = std::make_unique<deskflow::FileTransferManager>(
      m_localName, false,
      [this](const std::string &peer, const deskflow::FileTransferMessage &message) {
        return sendFileTransferMessage(peer, message);
      }
  );
  deskflow::fileTransferIpc::configure(*m_fileTransfer);
  deskflow::fileTransferIpc::attach(m_events);
  m_clipboardFiles = std::make_unique<deskflow::ClipboardFileSync>(
      *m_fileTransfer, deskflow::fileTransferIpc::clipboardFilesLimit(),
      [this](ClipboardID id, const IClipboard *clipboard) { m_computer->setClipboard(id, clipboard); }
  );
  m_events->addHandler(
      EventTypes::FileTransferCommand, deskflow::fileTransferIpc::eventTarget(),
      [this](const auto &e) { handleFileTransferCommand(e); }
  );
}

ClientHub::~ClientHub()
{
  auto *target = m_computer->getEventTarget();
  m_events->removeHandler(EventTypes::ComputerSuspend, target);
  m_events->removeHandler(EventTypes::ComputerResume, target);
  m_events->removeHandler(EventTypes::ComputerShapeChanged, target);
  m_events->removeHandler(EventTypes::ClipboardGrabbed, target);
  m_events->removeHandler(EventTypes::FileTransferCommand, deskflow::fileTransferIpc::eventTarget());
  deskflow::fileTransferIpc::attach(nullptr);
}

//
// client lifetime
//

void ClientHub::addClient(Client *client)
{
  m_sessions[m_nextId++].client = client;
}

void ClientHub::removeClient(Client *client)
{
  const auto id = idOf(client);
  if (id == InputArbiter::kNoSession)
    return;
  if (m_sessions.at(id).ready)
    sessionEnded(client);
  m_sessions.erase(id);
}

void ClientHub::sessionReady(Client *client)
{
  auto *session = find(idOf(client));
  if (session == nullptr || session->ready)
    return;

  session->ready = true;
  if (m_readyCount++ == 0) {
    m_computer->enable();
  }
  m_arbiter.addSession(idOf(client));
}

void ClientHub::sessionEnded(Client *client)
{
  const auto id = idOf(client);
  auto *session = find(id);
  if (session == nullptr || !session->ready)
    return;

  // hand the computer to someone else before it is switched off
  m_arbiter.removeSession(id);
  session->ready = false;
  session->options.clear();
  for (auto &stashed : session->stashed)
    stashed.reset();

  // nothing can be sent or finished through a server that is gone
  m_fileTransfer->peerDisconnected(linkOf(id));
  const auto peers = session->peers;
  session->peers.clear();
  for (const auto &peer : peers) {
    bool reachable = false;
    for (const auto &[otherId, other] : m_sessions)
      reachable = reachable || other.peers.contains(peer);
    if (!reachable)
      m_fileTransfer->peerDisconnected(peer.toStdString());
  }
  publishPeers();

  if (m_optionsFrom == id)
    m_optionsFrom = InputArbiter::kNoSession;
  if (--m_readyCount == 0) {
    m_entered = InputArbiter::kNoSession;
    m_computer->disable();
  }
}

const Client *ClientHub::controller() const
{
  const auto it = m_sessions.find(m_arbiter.owner());
  return it == m_sessions.end() ? nullptr : it->second.client;
}

bool ClientHub::mayControl(const Client *client) const
{
  const auto owner = m_arbiter.owner();
  return owner == InputArbiter::kNoSession || owner == idOf(client);
}

//
// messages from a server
//

void ClientHub::enter(Client *client, int32_t x, int32_t y, KeyModifierMask mask)
{
  m_arbiter.enter(idOf(client), x, y, mask);
}

void ClientHub::leave(Client *client)
{
  m_arbiter.leave(idOf(client));
}

void ClientHub::keyDown(Client *client, KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang)
{
  if (m_arbiter.input(idOf(client)))
    m_computer->keyDown(id, mask, button, lang);
}

void ClientHub::keyRepeat(
    Client *client, KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang
)
{
  if (m_arbiter.input(idOf(client)))
    m_computer->keyRepeat(id, mask, count, button, lang);
}

void ClientHub::keyUp(Client *client, KeyID id, KeyModifierMask mask, KeyButton button)
{
  if (m_arbiter.input(idOf(client), false))
    m_computer->keyUp(id, mask, button);
}

void ClientHub::mouseDown(Client *client, ButtonID id)
{
  if (m_arbiter.input(idOf(client)))
    m_computer->mouseDown(id);
}

void ClientHub::mouseUp(Client *client, ButtonID id)
{
  if (m_arbiter.input(idOf(client), false))
    m_computer->mouseUp(id);
}

void ClientHub::mouseMove(Client *client, int32_t x, int32_t y)
{
  if (m_arbiter.mouseMove(idOf(client), x, y))
    m_computer->mouseMove(x, y);
}

void ClientHub::mouseRelativeMove(Client *client, int32_t dx, int32_t dy)
{
  if (m_arbiter.input(idOf(client)))
    m_computer->mouseRelativeMove(dx, dy);
}

void ClientHub::mouseWheel(Client *client, int32_t xDelta, int32_t yDelta)
{
  if (m_arbiter.input(idOf(client)))
    m_computer->mouseWheel(xDelta, yDelta);
}

void ClientHub::screensaver(Client *client, bool activate)
{
  // follows the server in control; it never claims the computer
  if (mayControl(client))
    m_computer->screensaver(activate);
}

void ClientHub::resetOptions(Client *client)
{
  auto *session = find(idOf(client));
  if (session == nullptr)
    return;
  session->options.clear();
  if (mayControl(client)) {
    m_computer->resetOptions();
    m_optionsFrom = idOf(client);
  }
}

void ClientHub::setOptions(Client *client, const OptionsList &options)
{
  auto *session = find(idOf(client));
  if (session == nullptr)
    return;
  session->options.insert(session->options.end(), options.begin(), options.end());
  if (mayControl(client)) {
    if (m_optionsFrom != idOf(client))
      applyOptions(*session);
    else
      m_computer->setOptions(options);
    m_optionsFrom = idOf(client);
  }
}

void ClientHub::setClipboard(Client *client, ClipboardID id, const IClipboard *clipboard)
{
  const auto sessionId = idOf(client);
  auto *session = find(sessionId);
  if (session == nullptr || id >= kClipboardEnd)
    return;

  // a server pushes its clipboard as its cursor arrives; while another is in control
  // that would pull the clipboard from under the user, so keep it until it takes over
  if (!mayControl(client)) {
    LOG_DEBUG("keeping clipboard %d from a server that is not in control", id);
    session->stashed[id].emplace();
    Clipboard::copy(&*session->stashed[id], clipboard);
    return;
  }
  session->stashed[id].reset();
  applyClipboard(sessionId, id, clipboard);
}

void ClientHub::grabClipboard(Client *client, ClipboardID id)
{
  if (!mayControl(client))
    return;
  m_computer->grabClipboard(id);
  m_clipboardFiles->localChanged(id);
}

void ClientHub::applyClipboard(InputArbiter::SessionId from, ClipboardID id, const IClipboard *clipboard)
{
  m_clipboardFiles->incoming(id, clipboard);

  if (!m_shareClipboard)
    return;

  // pass it on as if it had been copied here
  for (const auto &[otherId, other] : m_sessions) {
    if (otherId != from && other.ready)
      other.client->shareClipboard(id);
  }
}

void ClientHub::handleClipboardGrabbed(const Event &event)
{
  const auto *info = static_cast<const IComputer::ClipboardInfo *>(event.getData());

  // whatever was being fetched for the old clipboard no longer belongs on it
  m_clipboardFiles->localChanged(info->m_id);
  forEachReady([info](Client *client) { client->handleClipboardGrabbed(info->m_id); });
}

//
// the arbiter's decisions
//

void ClientHub::enterComputer(InputArbiter::SessionId id, int32_t x, int32_t y, KeyModifierMask mask)
{
  auto *session = find(id);
  if (session == nullptr)
    return;

  if (m_entered != id) {
    if (m_sessions.size() > 1) {
      LOG_INFO("server \"%s\" is now in control", session->client->getServerName().c_str());
    }
    // another server's options and clipboard take over with its cursor
    if (m_optionsFrom != id) {
      applyOptions(*session);
      m_optionsFrom = id;
    }
    for (ClipboardID clipboard = 0; clipboard < kClipboardEnd; ++clipboard) {
      if (auto &stashed = session->stashed[clipboard]; stashed) {
        applyClipboard(id, clipboard, &*stashed);
        stashed.reset();
      }
    }
  }

  const bool changed = m_entered != id;
  m_entered = id;
  m_computer->mouseMove(x, y);
  m_computer->enter(mask);
  if (changed && m_controlChanged)
    m_controlChanged();
}

void ClientHub::leaveComputer(InputArbiter::SessionId id)
{
  if (m_entered != id)
    return;

  // leaving also lets go of every key and button the server was holding down
  m_computer->leave();
  m_entered = InputArbiter::kNoSession;
  if (m_controlChanged)
    m_controlChanged();
}

void ClientHub::controlLost(InputArbiter::SessionId id)
{
  if (auto *session = find(id); session != nullptr) {
    session->client->controlLost();
  }
}

void ClientHub::applyOptions(const Session &session)
{
  m_computer->resetOptions();
  if (!session.options.empty())
    m_computer->setOptions(session.options);
}

//
// file transfer
//

void ClientHub::handleFileTransferMessage(Client *client, const deskflow::FileTransferMessage &message)
{
  // the user may have picked another folder in the GUI since the last transfer
  if (message.type == deskflow::FileTransferMessage::Type::Start)
    m_fileTransfer->setDownloadDir(deskflow::fileTransferIpc::downloadDir());
  m_fileTransfer->handleMessage(linkOf(idOf(client)), message);
}

void ClientHub::setFilePeers(Client *client, const QStringList &names)
{
  if (auto *session = find(idOf(client)); session != nullptr) {
    session->peers = names;
    session->peers.removeAll(QString::fromStdString(m_localName));
    publishPeers();
  }
}

std::string ClientHub::linkOf(InputArbiter::SessionId id) const
{
  return kLinkPrefix + std::to_string(id);
}

bool ClientHub::sendFileTransferMessage(const std::string &peer, const deskflow::FileTransferMessage &message)
{
  Session *route = nullptr;
  if (peer.starts_with(kLinkPrefix)) {
    route = find(std::atoi(peer.c_str() + kLinkPrefix.size()));
  } else {
    // the server in control first, should two servers know a computer of the same name
    const auto name = QString::fromStdString(peer);
    if (auto *owner = find(m_arbiter.owner()); owner != nullptr && owner->peers.contains(name))
      route = owner;
    for (auto it = m_sessions.begin(); route == nullptr && it != m_sessions.end(); ++it) {
      if (it->second.peers.contains(name))
        route = &it->second;
    }
    // with a single server everything goes through it, as before peer lists existed
    if (route == nullptr && m_sessions.size() == 1)
      route = &m_sessions.begin()->second;
  }
  return route != nullptr && route->ready && route->client->sendFileTransferMessage(message);
}

void ClientHub::publishPeers() const
{
  QStringList peers;
  for (const auto &[id, session] : m_sessions) {
    for (const auto &peer : session.peers) {
      if (!peers.contains(peer))
        peers.append(peer);
    }
  }
  deskflow::fileTransferIpc::sendPeers(m_localName, peers);
}

void ClientHub::handleFileTransferCommand(const Event &event)
{
  const auto *command = static_cast<const deskflow::fileTransferIpc::Command *>(event.getDataObject());
  if (command->action == deskflow::fileTransferIpc::Command::Action::Cancel) {
    m_fileTransfer->cancel(command->id);
  } else {
    m_fileTransfer->sendFiles(command->target, command->paths);
  }
}

//
// helpers
//

InputArbiter::SessionId ClientHub::idOf(const Client *client) const
{
  for (const auto &[id, session] : m_sessions) {
    if (session.client == client)
      return id;
  }
  return InputArbiter::kNoSession;
}

ClientHub::Session *ClientHub::find(InputArbiter::SessionId id)
{
  const auto it = m_sessions.find(id);
  return it == m_sessions.end() ? nullptr : &it->second;
}

void ClientHub::forEachReady(const std::function<void(Client *)> &func) const
{
  for (const auto &[id, session] : m_sessions) {
    if (session.ready)
      func(session.client);
  }
}

} // namespace deskflow::client
