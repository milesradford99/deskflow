/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <QtGlobal>

#include <cstdint>
#include <functional>
#include <map>

namespace deskflow::client {

//! Decides which of several connected servers controls this computer
/*!
A client connected to more than one server gets an enter from each of them
whenever their cursor crosses onto it. Only one can drive the keyboard and
mouse at a time: the owner. An enter from a server is its claim, a leave or a
disconnect gives it up, and input from anyone else is dropped.

Another server takes over when the owner has been idle for the takeover delay:
- a negative delay means never (first come, first served),
- zero means at once (the latest server wins),
- otherwise the owner must have sent nothing for that many milliseconds.

A server whose claim is turned down, or whose control is taken over, is told
through Delegate::controlLost() so it can take its cursor back.
*/
class InputArbiter
{
public:
  using SessionId = int;
  static constexpr SessionId kNoSession = -1;

  //! What the arbiter does to the computer and tells the servers
  class Delegate
  {
  public:
    virtual ~Delegate() = default;

    //! \c session now controls the computer; its cursor is at \c x,y.
    virtual void enterComputer(SessionId session, int32_t x, int32_t y, KeyModifierMask mask) = 0;
    //! \c session no longer controls the computer (release any keys it holds).
    virtual void leaveComputer(SessionId session) = 0;
    //! \c session's server thinks its cursor is here but it isn't in control.
    virtual void controlLost(SessionId session) = 0;
  };

  //! Milliseconds from any fixed point, e.g. a monotonic clock.
  using Clock = std::function<qint64()>;

  InputArbiter(Delegate &delegate, Clock clock);

  void setTakeoverDelay(int milliseconds);
  int takeoverDelay() const
  {
    return m_takeoverDelay;
  }

  //! A server finished its handshake.
  void addSession(SessionId session);
  //! A server went away; if it was in control another waiting one takes over.
  void removeSession(SessionId session);

  //! A server's cursor entered this computer. Returns true if it is now in control.
  bool enter(SessionId session, int32_t x, int32_t y, KeyModifierMask mask);
  //! A server's cursor left this computer.
  void leave(SessionId session);

  //! Gate for an input event from \c session; true if it should reach the computer.
  /*!
  \c canClaim is false for events that end something (key or button up): those
  never take control, since the matching press went somewhere else.
  */
  bool input(SessionId session, bool canClaim = true);

  //! Gate for an absolute mouse move; also remembers where that server's cursor is.
  bool mouseMove(SessionId session, int32_t x, int32_t y);

  SessionId owner() const
  {
    return m_owner;
  }
  bool hasSession(SessionId session) const
  {
    return m_sessions.contains(session);
  }
  bool isEntered(SessionId session) const;

private:
  struct Session
  {
    bool entered = false;
    int32_t x = 0;
    int32_t y = 0;
    KeyModifierMask mask = 0;
    quint64 enteredOrder = 0; //!< higher entered more recently
  };

  bool canTakeOver() const;
  void grant(SessionId session);
  void release();
  void handOverToWaiting();

  Delegate &m_delegate;
  Clock m_clock;
  std::map<SessionId, Session> m_sessions;
  SessionId m_owner = kNoSession;
  qint64 m_ownerActivity = 0;
  int m_takeoverDelay = 0;
  quint64 m_enterCount = 0;
};

} // namespace deskflow::client
