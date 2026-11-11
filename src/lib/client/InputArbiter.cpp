/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/InputArbiter.h"

#include "base/Log.h"

namespace deskflow::client {

InputArbiter::InputArbiter(Delegate &delegate, Clock clock) : m_delegate(delegate), m_clock(std::move(clock))
{
  // do nothing
}

void InputArbiter::setTakeoverDelay(int milliseconds)
{
  m_takeoverDelay = milliseconds;
}

void InputArbiter::addSession(SessionId session)
{
  m_sessions.try_emplace(session);
}

void InputArbiter::removeSession(SessionId session)
{
  if (!m_sessions.contains(session))
    return;

  if (m_owner == session) {
    release();
  }
  m_sessions.erase(session);
  if (m_owner == kNoSession) {
    handOverToWaiting();
  }
}

bool InputArbiter::isEntered(SessionId session) const
{
  const auto it = m_sessions.find(session);
  return it != m_sessions.end() && it->second.entered;
}

bool InputArbiter::enter(SessionId session, int32_t x, int32_t y, KeyModifierMask mask)
{
  const auto it = m_sessions.find(session);
  if (it == m_sessions.end())
    return false;

  auto &state = it->second;
  state.entered = true;
  state.x = x;
  state.y = y;
  state.mask = mask;
  state.enteredOrder = ++m_enterCount;

  if (m_owner == session || m_owner == kNoSession || canTakeOver()) {
    grant(session);
    return true;
  }

  LOG_INFO("another server is in control, ignoring this one until it has been idle");
  m_delegate.controlLost(session);
  return false;
}

void InputArbiter::leave(SessionId session)
{
  const auto it = m_sessions.find(session);
  if (it == m_sessions.end())
    return;

  it->second.entered = false;
  if (m_owner == session) {
    release();
    handOverToWaiting();
  }
}

bool InputArbiter::input(SessionId session, bool canClaim)
{
  if (session == m_owner) {
    m_ownerActivity = m_clock();
    return true;
  }

  // only a server that believes its cursor is here may claim the computer
  if (!canClaim || !isEntered(session))
    return false;

  if (m_owner == kNoSession || canTakeOver()) {
    grant(session);
    m_ownerActivity = m_clock();
    return true;
  }
  return false;
}

bool InputArbiter::mouseMove(SessionId session, int32_t x, int32_t y)
{
  if (const auto it = m_sessions.find(session); it != m_sessions.end()) {
    it->second.x = x;
    it->second.y = y;
  }
  return input(session);
}

bool InputArbiter::canTakeOver() const
{
  if (m_takeoverDelay < 0)
    return false;
  return m_clock() - m_ownerActivity >= m_takeoverDelay;
}

void InputArbiter::grant(SessionId session)
{
  if (m_owner == session) {
    // a server entering again (e.g. after a wrap) just moves its cursor
    const auto &state = m_sessions.at(session);
    m_delegate.enterComputer(session, state.x, state.y, state.mask);
    m_ownerActivity = m_clock();
    return;
  }

  const auto previous = m_owner;
  if (previous != kNoSession) {
    LOG_INFO("server in control has been idle, handing the computer over");
    release();
    // its server still thinks the cursor is here
    if (isEntered(previous)) {
      m_delegate.controlLost(previous);
    }
  }

  m_owner = session;
  m_ownerActivity = m_clock();
  const auto &state = m_sessions.at(session);
  m_delegate.enterComputer(session, state.x, state.y, state.mask);
}

void InputArbiter::release()
{
  const auto previous = m_owner;
  m_owner = kNoSession;
  m_delegate.leaveComputer(previous);
}

void InputArbiter::handOverToWaiting()
{
  // the server that entered most recently gets it: its user is the likeliest to be waiting
  SessionId next = kNoSession;
  quint64 latest = 0;
  for (const auto &[id, state] : m_sessions) {
    if (state.entered && state.enteredOrder > latest) {
      next = id;
      latest = state.enteredOrder;
    }
  }
  if (next != kNoSession) {
    grant(next);
  }
}

} // namespace deskflow::client
