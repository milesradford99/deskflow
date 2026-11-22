/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/InputArbiter.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/MouseTypes.h"
#include "deskflow/OptionTypes.h"

#include <QElapsedTimer>
#include <QStringList>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

class Client;
class Event;
class IEventQueue;

namespace deskflow {
class ClipboardFileSync;
class Computer;
class FileTransferManager;
struct FileTransferMessage;
} // namespace deskflow

namespace deskflow::client {

//! What the clients of one computer share, one client per server
/*!
The core can stay connected to several servers at once. Each connection is a
Client; they all drive the same computer, so everything that belongs to the
computer lives here: enabling it, the events it raises, the clipboard files and
the file transfer manager. Input goes through an InputArbiter, so only the
server in control reaches the keyboard and mouse.

With a single server this is a pass-through and behaves like a plain client.
*/
class ClientHub : private InputArbiter::Delegate
{
public:
  ClientHub(IEventQueue *events, deskflow::Computer *computer, std::string localName);
  ~ClientHub() override;

  ClientHub(const ClientHub &) = delete;
  ClientHub &operator=(const ClientHub &) = delete;

  deskflow::Computer *computer() const
  {
    return m_computer;
  }
  ClipboardFileSync &clipboardFiles() const
  {
    return *m_clipboardFiles;
  }
  const InputArbiter &arbiter() const
  {
    return m_arbiter;
  }

  //! Called whenever a different server (or none) gets control.
  void setControlChangedHandler(std::function<void()> handler)
  {
    m_controlChanged = std::move(handler);
  }
  //! The client in control, if any.
  const Client *controller() const;

  //! @name Client lifetime
  //@{
  void addClient(Client *client);
  void removeClient(Client *client);
  //! The client finished its handshake and may drive the computer.
  void sessionReady(Client *client);
  //! The client lost its server.
  void sessionEnded(Client *client);
  //! True if the client is the one in control, or nobody is.
  bool mayControl(const Client *client) const;
  //@}

  //! @name Messages from a server, passed on or dropped by the arbiter
  //@{
  void enter(Client *client, int32_t x, int32_t y, KeyModifierMask mask);
  void leave(Client *client);
  void keyDown(Client *client, KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang);
  void
  keyRepeat(Client *client, KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang);
  void keyUp(Client *client, KeyID id, KeyModifierMask mask, KeyButton button);
  void mouseDown(Client *client, ButtonID id);
  void mouseUp(Client *client, ButtonID id);
  void mouseMove(Client *client, int32_t x, int32_t y);
  void mouseRelativeMove(Client *client, int32_t dx, int32_t dy);
  void mouseWheel(Client *client, int32_t xDelta, int32_t yDelta);
  void screensaver(Client *client, bool activate);
  void resetOptions(Client *client);
  void setOptions(Client *client, const OptionsList &options);
  void setClipboard(Client *client, ClipboardID id, const IClipboard *clipboard);
  void grabClipboard(Client *client, ClipboardID id);
  //@}

  //! @name File transfer, one manager for every server
  //@{
  void handleFileTransferMessage(Client *client, const deskflow::FileTransferMessage &message);
  void setFilePeers(Client *client, const QStringList &names);
  //@}

private:
  struct Session
  {
    Client *client = nullptr;
    bool ready = false;
    OptionsList options;
    QStringList peers;
    //! clipboards a server sent while another was in control, applied when it takes over
    std::array<std::optional<Clipboard>, kClipboardEnd> stashed;
  };

  // InputArbiter::Delegate
  void enterComputer(InputArbiter::SessionId session, int32_t x, int32_t y, KeyModifierMask mask) override;
  void leaveComputer(InputArbiter::SessionId session) override;
  void controlLost(InputArbiter::SessionId session) override;

  InputArbiter::SessionId idOf(const Client *client) const;
  Session *find(InputArbiter::SessionId id);
  void applyOptions(const Session &session);
  void applyClipboard(InputArbiter::SessionId from, ClipboardID id, const IClipboard *clipboard);
  void forEachReady(const std::function<void(Client *)> &func) const;

  std::string linkOf(InputArbiter::SessionId id) const;
  bool sendFileTransferMessage(const std::string &peer, const deskflow::FileTransferMessage &message);
  void publishPeers() const;
  void handleFileTransferCommand(const Event &event);
  void handleClipboardGrabbed(const Event &event);

  IEventQueue *m_events;
  deskflow::Computer *m_computer;
  std::string m_localName;
  QElapsedTimer m_clock;
  InputArbiter m_arbiter;
  std::map<InputArbiter::SessionId, Session> m_sessions;
  InputArbiter::SessionId m_nextId = 1;
  int m_readyCount = 0;
  //! the session whose enter the computer last saw
  InputArbiter::SessionId m_entered = InputArbiter::kNoSession;
  //! the session whose options the computer has
  InputArbiter::SessionId m_optionsFrom = InputArbiter::kNoSession;
  bool m_shareClipboard = false;
  std::function<void()> m_controlChanged;

  std::unique_ptr<deskflow::FileTransferManager> m_fileTransfer;
  std::unique_ptr<deskflow::ClipboardFileSync> m_clipboardFiles;
};

} // namespace deskflow::client
