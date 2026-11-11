/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include "deskflow/FileTransfer.h"

#include <QString>
#include <QStringList>

#include <string>

class IEventQueue;

//! Glue between the file transfer manager on the core thread and the GUI
/*!
The GUI asks for transfers over the core IPC socket, which is served on the Qt
main thread. Requests are turned into events for the core thread, where the
client or server that owns the manager handles them. Progress flows back the
other way as IPC broadcasts.
*/
namespace deskflow::fileTransferIpc {

//! A request from the GUI, delivered as the data object of EventTypes::FileTransferCommand
class Command : public EventData
{
public:
  enum class Action
  {
    Send,
    Cancel
  };

  Action action = Action::Send;
  std::string target;
  QStringList paths;
  uint32_t id = 0;
};

//! Target of the FileTransferCommand events.
void *eventTarget();

//! Start (or stop, with nullptr) delivering GUI requests to \c events. Core thread.
void attach(IEventQueue *events);

//! Handle an IPC command if it is a file transfer one. Any thread.
bool postCommand(const QString &command, const QString &args);

//! Create a manager's folders from the settings and empty the clipboard file cache.
void configure(FileTransferManager &manager);

//! Largest clipboard file set, in bytes, that is fetched without asking; 0 disables it.
quint64 clipboardFilesLimit();

//! Tell the GUI about a transfer.
void sendProgress(const FileTransferManager::Progress &progress);

//! Tell the GUI which computers files can be sent to.
void sendPeers(const std::string &self, const QStringList &peers);

} // namespace deskflow::fileTransferIpc
