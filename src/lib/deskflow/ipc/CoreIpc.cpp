/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpc.h"

#include "CoreIpcServer.h"
#include "deskflow/KeyDisplay.h"

#include <QMetaEnum>
#include <QUrl>

#include <atomic>

namespace {
// Read from the core thread on every key press, written from the main (ipc) thread.
std::atomic_bool s_keyOverlayEnabled{false};
} // namespace

void ipcSendToClient(const QString &command, const QString &args)
{
  // Queued because callers may not be on the main thread,
  // and QLocalSocket can only be written to from its owning thread.
  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server, [command, args] { deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(command, args); },
      Qt::QueuedConnection
  );
}

void ipcSendConnectionState(deskflow::core::ConnectionState state)
{
  const auto metaEnum = QMetaEnum::fromType<deskflow::core::ConnectionState>();
  ipcSendToClient(QStringLiteral("connectionState"), metaEnum.valueToKey(static_cast<int>(state)));
}

void ipcSendActivity(const QString &type, const QStringList &fields)
{
  if (!deskflow::core::ipc::CoreIpcServer::hasInstance())
    return;

  QStringList parts;
  parts.reserve(fields.size() + 1);
  parts.append(QString::fromLatin1(QUrl::toPercentEncoding(type)));
  for (const auto &field : fields)
    parts.append(QString::fromLatin1(QUrl::toPercentEncoding(field)));
  const auto args = parts.join(QLatin1Char(','));

  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server,
      [args] {
        deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(
            QStringLiteral("activity"), args, /*queueIfNoClients*/ false
        );
      },
      Qt::QueuedConnection
  );
}

void ipcSendKeyPress(KeyID id, KeyModifierMask mask)
{
  if (!s_keyOverlayEnabled.load(std::memory_order_relaxed) || !deskflow::core::ipc::CoreIpcServer::hasInstance())
    return;

  const auto display = deskflow::formatKeyForDisplay(id, mask);
  if (display.label.isEmpty())
    return;

  // `t:` marks text that can be joined into a word, `k:` a shortcut or special key.
  // Percent-encoded because the ipc protocol splits on '=' and newlines.
  const auto args = QString::fromLatin1(
      QUrl::toPercentEncoding((display.isText ? QStringLiteral("t:") : QStringLiteral("k:")) + display.label)
  );

  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server,
      [args] {
        if (!s_keyOverlayEnabled.load(std::memory_order_relaxed))
          return;
        deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(
            QStringLiteral("key"), args, /*queueIfNoClients*/ false, /*logContent*/ false
        );
      },
      Qt::QueuedConnection
  );
}

bool ipcKeyOverlayEnabled()
{
  return s_keyOverlayEnabled.load(std::memory_order_relaxed);
}

void ipcSetKeyOverlayEnabled(bool enabled)
{
  s_keyOverlayEnabled.store(enabled, std::memory_order_relaxed);
}
