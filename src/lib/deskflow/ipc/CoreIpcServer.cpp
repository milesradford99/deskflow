/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "CoreIpc.h"

#include "base/Log.h"
#include "common/Constants.h"
#include "deskflow/FileTransferIpc.h"
#include "deskflow/RemoteViewIpc.h"

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent) : IpcServer(parent, kCoreIpcName, QStringLiteral("core"))
{
  assert(s_instance == nullptr);
  s_instance = this;

  // Stop producing key presses as soon as no GUI is listening, even if it never said "off".
  connect(this, &IpcServer::clientCountChanged, this, [](int count) {
    if (count == 0)
      deskflow::remoteViewIpc::postStopAll(); // nobody left to show the streams to
    if (count == 0 && ipcKeyOverlayEnabled()) {
      LOG_DEBUG("core ipc server has no clients, key overlay off");
      ipcSetKeyOverlayEnabled(false);
    }
  });
}

bool CoreIpcServer::hasInstance()
{
  return s_instance != nullptr;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (deskflow::fileTransferIpc::postCommand(command, parts.size() >= 2 ? parts.at(1) : QString()))
    return;
  if (deskflow::remoteViewIpc::postCommand(command, parts.size() >= 2 ? parts.at(1) : QString()))
    return;

  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  if (command == QStringLiteral("overlayKeys")) {
    const bool enabled = parts.size() >= 2 && parts.at(1) == QStringLiteral("on");
    LOG_DEBUG("core ipc server key overlay %s", enabled ? "on" : "off");
    ipcSetKeyOverlayEnabled(enabled);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
