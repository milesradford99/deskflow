/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"

#include <QJsonObject>
#include <QString>

#include <string>

class IEventQueue;

//! Glue between a server's remote views on the core thread and the GUI that shows them
/*!
The GUI's requests (start/stop a view, frame shown, input typed into a viewer)
arrive on the IPC thread and become events for the core thread. Frames and
state go the other way as IPC broadcasts.
*/
namespace deskflow::remoteViewIpc {

//! A request from the GUI, delivered as the data object of EventTypes::RemoteViewCommand
class Command : public EventData
{
public:
  enum class Action
  {
    View,   //!< payload: settings for the client
    Ack,    //!< payload: {sequence}
    Input,  //!< payload: {t, ...}
    StopAll //!< the GUI went away
  };
  Action action = Action::View;
  std::string client;
  QJsonObject payload;
};

void *eventTarget();
//! Start (or stop, with nullptr) delivering GUI requests to \c events. Core thread.
void attach(IEventQueue *events);
//! Handle an IPC command if it is a remote view one. Any thread.
bool postCommand(const QString &command, const QString &args);
//! The GUI disconnected: stop every stream. Any thread.
void postStopAll();

//! Pass a frame piece from \c client to the GUI. Any thread.
void sendFrame(const std::string &client, const std::string &piece);
//! Pass \c client's remote view state to the GUI (\c info is the client's JSON, or built here). Any thread.
void sendInfo(const std::string &client, const QJsonObject &info);

} // namespace deskflow::remoteViewIpc
