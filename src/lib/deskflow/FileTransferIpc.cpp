/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferIpc.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/IpcPayload.h"
#include "common/Settings.h"
#include "deskflow/ipc/CoreIpc.h"

#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>

#include <mutex>

namespace deskflow::fileTransferIpc {

namespace {

std::mutex s_mutex;
IEventQueue *s_events = nullptr;

QString stateName(FileTransferManager::State state)
{
  using enum FileTransferManager::State;
  switch (state) {
  case Active:
    return QStringLiteral("active");
  case Completed:
    return QStringLiteral("completed");
  case Cancelled:
    return QStringLiteral("cancelled");
  default:
    return QStringLiteral("failed");
  }
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
  const bool isSend = command == ipc::kSendFiles;
  if (!isSend && command != ipc::kCancelTransfer)
    return false;

  const auto payload = ipc::decodePayload(args);
  auto *data = new Command; // NOSONAR - owned by the event
  if (isSend) {
    data->action = Command::Action::Send;
    data->target = payload["target"].toString().toStdString();
    const auto paths = payload["paths"].toArray();
    for (const auto &path : paths)
      data->paths.append(path.toString());
  } else {
    data->action = Command::Action::Cancel;
    data->id = static_cast<uint32_t>(payload["id"].toInteger());
  }

  std::scoped_lock lock{s_mutex};
  if (s_events == nullptr) {
    LOG_WARN("file transfer: not connected, ignoring request from the gui");
    delete data;
    return true;
  }
  s_events->addEvent(Event(EventTypes::FileTransferCommand, eventTarget(), data));
  return true;
}

void configure(FileTransferManager &manager)
{
  auto downloads = Settings::value(Settings::Core::DownloadDir).toString();
  if (downloads.isEmpty())
    downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (downloads.isEmpty())
    downloads = QDir::homePath();
  manager.setDownloadDir(downloads);

  // one cache per computer name, so a server and client on one machine don't share it
  const auto name = QString::fromStdString(manager.localName()).replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
  manager.setCacheDir(QStringLiteral("%1/deskflow-clipboard-files/%2").arg(QDir::tempPath(), name));
  manager.clearCache();

  manager.setProgressHandler(&sendProgress);
  LOG_DEBUG("file transfer: received files go to %s", qPrintable(downloads));
}

quint64 clipboardFilesLimit()
{
  return Settings::value(Settings::Core::ClipboardFilesLimit).toULongLong() * 1024 * 1024;
}

void sendProgress(const FileTransferManager::Progress &progress)
{
  QJsonObject payload;
  payload["id"] = static_cast<qint64>(progress.id);
  payload["incoming"] = progress.incoming;
  payload["clipboard"] = progress.kind == FileTransferManager::Kind::Clipboard;
  payload["state"] = stateName(progress.state);
  payload["peer"] = progress.peer;
  payload["label"] = progress.label;
  payload["total"] = static_cast<qint64>(progress.total);
  payload["done"] = static_cast<qint64>(progress.done);
  payload["detail"] = progress.detail;
  // progress is only worth showing live; don't queue it for a GUI that isn't there
  ipcSendToClient(ipc::kFileTransfer, ipc::encodePayload(payload), false);
}

void sendPeers(const std::string &self, const QStringList &peers)
{
  QJsonObject payload;
  payload["self"] = QString::fromStdString(self);
  payload["peers"] = QJsonArray::fromStringList(peers);
  ipcSendToClient(ipc::kFilePeers, ipc::encodePayload(payload), false);
}

} // namespace deskflow::fileTransferIpc
