/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>

namespace deskflow::gui {
class CoreProcess;
}

namespace deskflow::gui::remote {

class RemoteViewWindow;

//! Opens remote view windows on a server and routes frames and state to them
class RemoteViewManager : public QObject
{
  Q_OBJECT
public:
  explicit RemoteViewManager(CoreProcess &core, QObject *parent = nullptr);
  ~RemoteViewManager() override;

  //! Show \c client's screen (or bring its window forward)
  void open(const QString &client);
  //! Clients connected to this server right now
  QStringList clients() const
  {
    return m_clients;
  }

Q_SIGNALS:
  void clientsChanged(const QStringList &clients);

private:
  void setClients(const QStringList &clients);

  CoreProcess &m_core;
  QStringList m_clients;
  QMap<QString, QPointer<RemoteViewWindow>> m_windows;
};

} // namespace deskflow::gui::remote
