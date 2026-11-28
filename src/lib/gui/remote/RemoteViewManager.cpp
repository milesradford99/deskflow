/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RemoteViewManager.h"

#include "RemoteViewWindow.h"

#include "common/IpcPayload.h"
#include "gui/core/CoreProcess.h"

#include <QJsonObject>

namespace deskflow::gui::remote {

RemoteViewManager::RemoteViewManager(CoreProcess &core, QObject *parent) : QObject(parent), m_core{core}
{
  connect(&m_core, &CoreProcess::remoteFrame, this, [this](const QString &client, const QByteArray &piece) {
    if (auto window = m_windows.value(client); window)
      window->handlePiece(piece);
  });
  connect(&m_core, &CoreProcess::remoteInfo, this, [this](const QJsonObject &info) {
    if (auto window = m_windows.value(info.value("client").toString()); window)
      window->handleInfo(info);
  });
  connect(&m_core, &CoreProcess::connectedClientsChanged, this, &RemoteViewManager::setClients);
  connect(&m_core, &CoreProcess::connectionStateChanged, this, [this](deskflow::core::ConnectionState state) {
    if (state == deskflow::core::ConnectionState::Disconnected || state == deskflow::core::ConnectionState::Listening)
      setClients({});
  });
}

RemoteViewManager::~RemoteViewManager()
{
  for (const auto &window : std::as_const(m_windows)) {
    if (window)
      delete window.data();
  }
}

void RemoteViewManager::open(const QString &client)
{
  auto window = m_windows.value(client);
  if (!window) {
    window = new RemoteViewWindow(client); // NOSONAR - deletes itself on close
    m_windows.insert(client, window);
    connect(window, &RemoteViewWindow::command, this, [this](const QString &command, const QJsonObject &payload) {
      m_core.sendRemoteView(command, payload);
    });
    connect(window, &RemoteViewWindow::closed, this, [this](const QString &name) { m_windows.remove(name); });
    window->setConnected(m_clients.contains(client));
    if (m_clients.contains(client))
      m_core.sendRemoteView(deskflow::ipc::kRemoteView, window->request(true));
  }
  window->show();
  window->raise();
  window->activateWindow();
}

void RemoteViewManager::setClients(const QStringList &clients)
{
  if (clients == m_clients)
    return;
  m_clients = clients;
  for (const auto &window : std::as_const(m_windows)) {
    if (window)
      window->setConnected(m_clients.contains(window->client()));
  }
  Q_EMIT clientsChanged(m_clients);
}

} // namespace deskflow::gui::remote
