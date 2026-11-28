/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Remote view on the server: the GUI shows a client's screen in a window and
// whatever is typed or clicked there goes to that client.
//
// The viewer drives the client without moving our cursor there: it sends the
// client an enter of its own (with a new sequence number, so clipboards work
// as usual) when the viewer is used, and a leave when the viewer lets go.
// Our cursor crossing onto that client takes over as usual.

#include "server/Server.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/AppUtil.h"
#include "deskflow/RemoteViewInput.h"
#include "deskflow/RemoteViewIpc.h"
#include "server/BaseClientProxy.h"
#include "server/PrimaryClient.h"

#include <QJsonDocument>

namespace input = deskflow::remoteViewInput;
using Command = deskflow::remoteViewIpc::Command;

void Server::handleRemoteViewFrame(BaseClientProxy *client, const std::string &piece)
{
  if (!m_remoteViews.contains(client))
    return; // a frame still on its way after the view was closed
  deskflow::remoteViewIpc::sendFrame(getName(client), piece);
}

void Server::handleRemoteViewInfo(BaseClientProxy *client, const std::string &json)
{
  const auto info = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
  LOG_DEBUG(
      "remote view of \"%s\": %s %s", getName(client).c_str(), qPrintable(info.value("state").toString()),
      qPrintable(info.value("detail").toString())
  );
  deskflow::remoteViewIpc::sendInfo(getName(client), info);
}

void Server::handleRemoteViewCommand(const Event &event)
{
  const auto *command = static_cast<const Command *>(event.getDataObject());

  if (command->action == Command::Action::StopAll) {
    while (!m_remoteViews.empty())
      stopRemoteView(m_remoteViews.begin()->first, true);
    return;
  }

  const auto it = m_clients.find(command->client);
  if (it == m_clients.end() || it->second == m_primaryClient) {
    if (command->action == Command::Action::View)
      deskflow::remoteViewIpc::sendInfo(command->client, QJsonObject{{"state", "stopped"}});
    return;
  }
  BaseClientProxy *client = it->second;

  switch (command->action) {
  case Command::Action::View: {
    if (!command->payload.value("on").toBool()) {
      stopRemoteView(client, true);
      return;
    }
    const auto json = QJsonDocument(command->payload).toJson(QJsonDocument::Compact).toStdString();
    if (!client->sendRemoteViewSettings(json)) {
      deskflow::remoteViewIpc::sendInfo(
          command->client, QJsonObject{{"state", "unsupported"}, {"detail", "the client runs an older Deskflow"}}
      );
      return;
    }
    if (!m_remoteViews.contains(client))
      LOG_INFO("remote view of \"%s\" opened", command->client.c_str());
    m_remoteViews.try_emplace(client);
    break;
  }
  case Command::Action::Ack:
    if (m_remoteViews.contains(client))
      client->sendRemoteViewAck(static_cast<uint32_t>(command->payload.value("sequence").toInteger()));
    break;
  case Command::Action::Input:
    if (auto view = m_remoteViews.find(client); view != m_remoteViews.end())
      remoteViewInput(client, view->second, command->payload);
    break;
  default:
    break;
  }
}

void Server::stopRemoteView(BaseClientProxy *client, bool tellClient)
{
  auto view = m_remoteViews.find(client);
  if (view == m_remoteViews.end())
    return;
  remoteViewLeave(client, view->second);
  m_remoteViews.erase(view);
  if (tellClient)
    client->sendRemoteViewSettings(R"({"on":false})");
  LOG_INFO("remote view of \"%s\" closed", getName(client).c_str());
}

void Server::remoteViewEnter(BaseClientProxy *client, RemoteView &view, int32_t x, int32_t y)
{
  if (view.entered)
    return;
  view.entered = true;
  if (client == m_active)
    return; // our cursor is already there

  int32_t cx;
  int32_t cy;
  int32_t cw;
  int32_t ch;
  client->getShape(cx, cy, cw, ch);
  x = std::clamp(x, cx, cx + cw - 1);
  y = std::clamp(y, cy, cy + ch - 1);

  ++m_seqNum;
  LOG_DEBUG("remote view takes control of \"%s\" at %d,%d", getName(client).c_str(), x, y);
  client->enter(x, y, m_seqNum, m_primaryClient->getToggleMask(), false);
  if (m_enableClipboard) {
    // what was copied here can be pasted there, as when the cursor crosses over
    for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
      if (m_clipboards[id].m_clipboard.marshall().size() <= m_maximumClipboardSize * 1024)
        client->setClipboard(id, &m_clipboards[id].m_clipboard);
    }
  }
}

void Server::remoteViewLeave(BaseClientProxy *client, RemoteView &view)
{
  for (const auto button : view.buttons)
    client->mouseUp(button);
  for (const auto key : view.keys)
    client->keyUp(kKeyNone, 0, key);
  view.buttons.clear();
  view.keys.clear();
  if (!view.entered)
    return;
  view.entered = false;
  if (client != m_active) {
    LOG_DEBUG("remote view lets go of \"%s\"", getName(client).c_str());
    client->leave(); // the client sends back what was copied there
  }
}

bool Server::remoteViewControlDenied(const BaseClientProxy *client)
{
  auto view = m_remoteViews.find(const_cast<BaseClientProxy *>(client));
  if (view == m_remoteViews.end() || !view->second.entered || client == m_active)
    return false;
  LOG_INFO("\"%s\" is controlled by another server; the remote view only watches", getName(client).c_str());
  view->second.entered = false;
  view->second.keys.clear();
  view->second.buttons.clear();
  deskflow::remoteViewIpc::sendInfo(getName(client), QJsonObject{{"state", "controlled"}});
  return true;
}

void Server::remoteViewInput(BaseClientProxy *client, RemoteView &view, const QJsonObject &event)
{
  const auto type = event.value("t").toString();
  const auto x = static_cast<int32_t>(event.value("x").toInt());
  const auto y = static_cast<int32_t>(event.value("y").toInt());

  if (type == QStringLiteral("leave")) {
    remoteViewLeave(client, view);
    return;
  }
  if (type == QStringLiteral("enter")) {
    remoteViewEnter(client, view, x, y);
    return;
  }
  if (type == QStringLiteral("move")) {
    remoteViewEnter(client, view, x, y);
    client->mouseMove(x, y);
    return;
  }
  if (type == QStringLiteral("down") || type == QStringLiteral("up")) {
    const auto button = input::translateButton(event.value("b").toInt());
    if (button == kButtonNone)
      return;
    if (type == QStringLiteral("down")) {
      remoteViewEnter(client, view, x, y);
      client->mouseMove(x, y);
      client->mouseDown(button);
      view.buttons.insert(button);
    } else if (view.buttons.erase(button) > 0) {
      client->mouseUp(button);
    }
    return;
  }
  if (type == QStringLiteral("wheel")) {
    remoteViewEnter(client, view, x, y);
    client->mouseWheel(event.value("dx").toInt(), event.value("dy").toInt());
    return;
  }
  if (type == QStringLiteral("key")) {
    const auto key =
        input::translateKey(event.value("key").toInt(), event.value("text").toString(), event.value("mods").toInt());
    if (key.id == kKeyNone)
      return;
    // the same physical key must come up with the same button it went down with
    auto button = static_cast<KeyButton>(event.value("code").toInt() & 0xffff);
    if (button == 0)
      button = static_cast<KeyButton>((event.value("key").toInt() & 0x7fff) | 0x8000);
    const auto lang = AppUtil::instance().getCurrentLanguageCode();
    if (event.value("down").toBool()) {
      remoteViewEnter(client, view, x, y);
      if (view.keys.contains(button) && event.value("repeat").toBool()) {
        client->keyRepeat(key.id, key.mask, 1, button, lang);
      } else {
        client->keyDown(key.id, key.mask, button, lang);
        view.keys.insert(button);
      }
    } else if (view.keys.erase(button) > 0) {
      client->keyUp(key.id, key.mask, button);
    }
  }
}
