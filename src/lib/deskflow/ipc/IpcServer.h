/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>

class QLocalServer;
class QLocalSocket;

namespace deskflow::core::ipc {

class IpcServer : public QObject
{
  Q_OBJECT

public:
  explicit IpcServer(QObject *parent, const QString &serverName, const QString &typeName);
  ~IpcServer() override;

  void listen();
  /**
   * @brief Send a command to every connected client.
   * @param queueIfNoClients When true, the message is replayed to the next client that connects.
   *        Live activity (e.g. overlay events) should not be queued, it is only meaningful now.
   * @param logContent When false, the message text is never written to the log (e.g. key presses).
   */
  void broadcastCommand(
      const QString &command, const QString &args = "", bool queueIfNoClients = true, bool logContent = true
  );

  int clientCount() const
  {
    return static_cast<int>(m_clients.size());
  }

Q_SIGNALS:
  void logLevelChanged(const QString &logLevel);
  void configFileChanged(const QString &configFile);
  void startProcessRequested();
  void stopProcessRequested();
  void clearSettingsRequested();
  void clientCountChanged(int count);

protected:
  /**!
   * Write a message to the client socket and append a newline character.
   *
   * \param clientSocket The client socket to write to.
   * \param message The message to write (without trailing newline).
   * \param logContent When false, the message text is not logged.
   */
  void writeToClientSocket(QLocalSocket *&clientSocket, const QString &message, bool logContent = true) const;

private:
  void processMessage(QLocalSocket *clientSocket, const QString &message);
  virtual void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) = 0;
  void handleNewConnection();
  void handleReadyRead();
  void handleDisconnected();
  void handleErrorOccurred();

  QLocalServer *m_server;
  QSet<QLocalSocket *> m_clients;
  QString m_serverName;
  QStringList m_pendingMessages;
  QHash<QLocalSocket *, QByteArray> m_readBuffers;
  QByteArray m_typeName;
};

} // namespace deskflow::core::ipc
