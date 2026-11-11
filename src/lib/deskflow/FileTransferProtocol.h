/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransfer.h"

#include <QStringList>

namespace deskflow {
class IStream;
}

//! Wire encoding of file transfer messages (protocol 1.9)
namespace deskflow::fileTransferProtocol {

//! True if the 4 byte message code belongs to the file transfer family.
bool isMessage(const uint8_t *code);

//! Read the rest of a message whose code was already consumed. Returns false on a malformed message.
bool read(IStream *stream, const uint8_t *code, FileTransferMessage &message);

void write(IStream *stream, const FileTransferMessage &message);

//! The computers a client can send files to: the server's name first, then every client.
void writePeerList(IStream *stream, const QStringList &names);
bool isPeerList(const uint8_t *code);
bool readPeerList(IStream *stream, QStringList &names);

} // namespace deskflow::fileTransferProtocol
