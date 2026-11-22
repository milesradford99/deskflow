/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferProtocol.h"

#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <cstring>

namespace deskflow::fileTransferProtocol {

namespace {

using Type = FileTransferMessage::Type;

bool is(const uint8_t *code, const char *message)
{
  return std::memcmp(code, message, 4) == 0;
}

} // namespace

bool isMessage(const uint8_t *code)
{
  return is(code, kMsgDFileStart) || is(code, kMsgDFileData) || is(code, kMsgDFileAck) || is(code, kMsgDFileEnd) ||
         is(code, kMsgDFileRequest) || is(code, kMsgDFileDecline);
}

bool read(IStream *stream, const uint8_t *code, FileTransferMessage &message)
{
  message.value = 0;
  message.payload.clear();

  if (is(code, kMsgDFileStart)) {
    message.type = Type::Start;
    return ProtocolUtil::readf(stream, kMsgDFileStart + 4, &message.id, &message.payload);
  }
  if (is(code, kMsgDFileData)) {
    message.type = Type::Data;
    return ProtocolUtil::readf(stream, kMsgDFileData + 4, &message.id, &message.payload);
  }
  if (is(code, kMsgDFileAck)) {
    message.type = Type::Ack;
    return ProtocolUtil::readf(stream, kMsgDFileAck + 4, &message.id, &message.value);
  }
  if (is(code, kMsgDFileEnd)) {
    message.type = Type::End;
    return ProtocolUtil::readf(stream, kMsgDFileEnd + 4, &message.id, &message.value, &message.payload);
  }
  if (is(code, kMsgDFileRequest)) {
    message.type = Type::Request;
    return ProtocolUtil::readf(stream, kMsgDFileRequest + 4, &message.id, &message.payload);
  }
  if (is(code, kMsgDFileDecline)) {
    message.type = Type::Decline;
    return ProtocolUtil::readf(stream, kMsgDFileDecline + 4, &message.id, &message.payload);
  }
  return false;
}

void write(IStream *stream, const FileTransferMessage &message)
{
  switch (message.type) {
  case Type::Start:
    ProtocolUtil::writef(stream, kMsgDFileStart, message.id, &message.payload);
    break;
  case Type::Data:
    ProtocolUtil::writef(stream, kMsgDFileData, message.id, &message.payload);
    break;
  case Type::Ack:
    ProtocolUtil::writef(stream, kMsgDFileAck, message.id, message.value);
    break;
  case Type::End:
    ProtocolUtil::writef(stream, kMsgDFileEnd, message.id, message.value, &message.payload);
    break;
  case Type::Request:
    ProtocolUtil::writef(stream, kMsgDFileRequest, message.id, &message.payload);
    break;
  case Type::Decline:
    ProtocolUtil::writef(stream, kMsgDFileDecline, message.id, &message.payload);
    break;
  }
}

void writePeerList(IStream *stream, const QStringList &names)
{
  const auto list = names.join('\n').toStdString();
  ProtocolUtil::writef(stream, kMsgDFilePeers, &list);
}

bool isPeerList(const uint8_t *code)
{
  return is(code, kMsgDFilePeers);
}

bool readPeerList(IStream *stream, QStringList &names)
{
  std::string list;
  if (!ProtocolUtil::readf(stream, kMsgDFilePeers + 4, &list))
    return false;
  names = QString::fromStdString(list).split('\n', Qt::SkipEmptyParts);
  return true;
}

} // namespace deskflow::fileTransferProtocol
