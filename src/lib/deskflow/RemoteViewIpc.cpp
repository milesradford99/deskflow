/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/RemoteViewIpc.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/IpcPayload.h"
#include "deskflow/ipc/CoreIpc.h"
#include "deskflow/ipc/CoreIpcServer.h"

#include <mutex>

namespace deskflow::remoteViewIpc {

namespace {
std::mutex s_mutex;
IEventQueue *s_events = nullptr;

constexpr auto kBase64 = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

void post(Command *command)
{
  std::scoped_lock lock{s_mutex};
  if (s_events == nullptr) {
    delete command;
    return;
  }
  s_events->addEvent(Event(EventTypes::RemoteViewCommand, eventTarget(), command));
}
} // namespace

void *eventTarget()
{
  static char s_target = 0;
  return &s_target;
}

void attach(IEventQueue *events)
{
  std::scoped_lock lock{s_mutex};
  s_events = events;
}

bool postCommand(const QString &command, const QString &args)
{
  Command::Action action;
  if (command == ipc::kRemoteView)
    action = Command::Action::View;
  else if (command == ipc::kRemoteAck)
    action = Command::Action::Ack;
  else if (command == ipc::kRemoteInput)
    action = Command::Action::Input;
  else
    return false;

  auto *data = new Command; // NOSONAR - owned by the event
  data->action = action;
  data->payload = ipc::decodePayload(args);
  data->client = data->payload.value("client").toString().toStdString();
  data->payload.remove("client");
  post(data);
  return true;
}

void postStopAll()
{
  auto *data = new Command; // NOSONAR - owned by the event
  data->action = Command::Action::StopAll;
  post(data);
}

void sendFrame(const std::string &client, const std::string &piece)
{
  const auto args = QString::fromLatin1(QByteArray::fromStdString(client).toBase64(kBase64)) + QLatin1Char('.') +
                    QString::fromLatin1(QByteArray::fromStdString(piece).toBase64(kBase64));
  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server,
      [args] {
        deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(
            ipc::kRemoteFrame, args, /*queueIfNoClients*/ false, /*logContent*/ false
        );
      },
      Qt::QueuedConnection
  );
}

void sendInfo(const std::string &client, const QJsonObject &info)
{
  QJsonObject payload = info;
  payload["client"] = QString::fromStdString(client);
  ipcSendToClient(ipc::kRemoteInfo, ipc::encodePayload(payload), false);
}

} // namespace deskflow::remoteViewIpc
