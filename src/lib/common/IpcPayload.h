/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

//! Structured arguments for the line based core/GUI IPC
/*!
An IPC message is `command=args` on one line, so args can't hold newlines or
`=`. Anything richer than a plain word travels as URL-safe base64 of JSON.
*/
namespace deskflow::ipc {

inline QString encodePayload(const QJsonObject &object)
{
  const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

inline QJsonObject decodePayload(const QString &text)
{
  const auto json = QByteArray::fromBase64(text.toLatin1(), QByteArray::Base64UrlEncoding);
  return QJsonDocument::fromJson(json).object();
}

// commands, GUI -> core
inline const auto kSendFiles = QStringLiteral("sendFiles");           //!< {target, paths[]}
inline const auto kCancelTransfer = QStringLiteral("cancelTransfer"); //!< {id}

// commands, core -> GUI
inline const auto kFileTransfer = QStringLiteral("fileTransfer"); //!< progress, see FileTransferIpc
inline const auto kFilePeers = QStringLiteral("filePeers");       //!< {self, peers[]}
//! client mode: {servers: [{host, state, control}]}, state as in deskflow::core::ConnectionState
inline const auto kServers = QStringLiteral("servers");

} // namespace deskflow::ipc
