/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransfer.h"

#include "base/Log.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QStorageInfo>
#include <QtEndian>

#include <algorithm>

namespace deskflow {

namespace {

using Type = FileTransferMessage::Type;

const auto kStagePrefix = QStringLiteral(".deskflow-incoming-");
constexpr qint64 kFreeSpaceMargin = 16 * 1024 * 1024;
constexpr qint64 kReportIntervalMs = 100;

QByteArray toJson(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject fromJson(const std::string &text)
{
  return QJsonDocument::fromJson(QByteArray::fromRawData(text.data(), static_cast<qsizetype>(text.size()))).object();
}

QString kindName(FileTransferManager::Kind kind)
{
  return kind == FileTransferManager::Kind::Clipboard ? QStringLiteral("clip") : QStringLiteral("send");
}

//! Make a path from another computer safe to create below a folder we own; empty if it can't be.
QString sanitizeRelativePath(const QString &path)
{
  static const QSet<QString> kReserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2"};

  QStringList clean;
  const auto parts = path.split('/');
  for (auto part : parts) {
    if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
      return {};

    for (auto &c : part) {
      if (c.unicode() < 0x20 || QStringLiteral("<>:\"|?*\\").contains(c))
        c = '_';
    }

    // Windows silently drops trailing dots and spaces, which would alias other names
    while (part.endsWith('.') || part.endsWith(' '))
      part.chop(1);
    if (part.isEmpty())
      return {};

    if (kReserved.contains(part.section('.', 0, 0).toUpper()))
      part.prepend('_');
    clean.append(part);
  }
  return clean.join('/');
}

//! A name next to \c wanted in \c dir that doesn't exist yet: "a.txt", "a (2).txt", ...
QString uniquePath(const QString &dir, const QString &wanted)
{
  auto candidate = QStringLiteral("%1/%2").arg(dir, wanted);
  if (!QFileInfo::exists(candidate))
    return candidate;

  const auto dot = wanted.lastIndexOf('.');
  const bool hasSuffix = dot > 0 && !QFileInfo(candidate).isDir();
  const auto base = hasSuffix ? wanted.left(dot) : wanted;
  const auto suffix = hasSuffix ? wanted.mid(dot) : QString();
  for (int i = 2;; ++i) {
    candidate = QStringLiteral("%1/%2 (%3)%4").arg(dir, base).arg(i).arg(suffix);
    if (!QFileInfo::exists(candidate))
      return candidate;
  }
}

std::string fetchedKey(const std::string &origin, uint32_t token)
{
  return origin + "/" + std::to_string(token);
}

FileTransferManager::State stateFor(FileTransferStatus status)
{
  using enum FileTransferManager::State;
  switch (status) {
  case FileTransferStatus::Ok:
    return Completed;
  case FileTransferStatus::Cancelled:
    return Cancelled;
  default:
    return Failed;
  }
}

} // namespace

FileTransferManager::FileTransferManager(std::string localName, bool isRouter, SendFunc send)
    : m_localName(std::move(localName)),
      m_isRouter(isRouter),
      m_send(std::move(send)),
      m_offerToken(QRandomGenerator::global()->generate() | 1u)
{
  // do nothing
}

FileTransferManager::~FileTransferManager()
{
  // leave no half written downloads behind
  for (const auto &[id, transfer] : m_incoming) {
    transfer->file.close();
    QDir(transfer->stageDir).removeRecursively();
  }
}

void FileTransferManager::setProgressHandler(ProgressFunc handler)
{
  m_progress = std::move(handler);
}

void FileTransferManager::setClipboardFilesHandler(ClipboardFilesFunc handler)
{
  m_clipboardFiles = std::move(handler);
}

void FileTransferManager::setDownloadDir(const QString &dir)
{
  m_downloadDir = QDir::cleanPath(dir);
}

void FileTransferManager::setCacheDir(const QString &dir)
{
  m_cacheDir = QDir::cleanPath(dir);
}

void FileTransferManager::clearCache()
{
  m_fetched.clear();
  if (!m_cacheDir.isEmpty())
    QDir(m_cacheDir).removeRecursively();
}

size_t FileTransferManager::activeCount() const
{
  return m_outgoing.size() + m_incoming.size() + m_routes.size();
}

uint32_t FileTransferManager::newId() const
{
  for (;;) {
    const auto id = QRandomGenerator::global()->generate();
    if (id != 0 && !m_outgoing.contains(id) && !m_incoming.contains(id) && !m_routes.contains(id))
      return id;
  }
}

bool FileTransferManager::send(
    const std::string &peer, FileTransferMessage::Type type, uint32_t id, uint32_t value, std::string payload
)
{
  FileTransferMessage message;
  message.type = type;
  message.id = id;
  message.value = value;
  message.payload = std::move(payload);
  return m_send && m_send(peer, message);
}

void FileTransferManager::sendEnd(
    const std::string &peer, uint32_t id, FileTransferStatus status, const QString &reason
)
{
  send(peer, Type::End, id, static_cast<uint32_t>(status), reason.toStdString());
}

//
// sending
//

bool FileTransferManager::collectDirectory(
    const QString &dir, const QString &relative, std::vector<Entry> &entries, quint64 &total, size_t maxEntries
)
{
  const auto children =
      QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
  for (const auto &child : children) {
    if (entries.size() >= maxEntries)
      return false;

    Entry entry;
    entry.localPath = child.absoluteFilePath();
    entry.relativePath = QStringLiteral("%1/%2").arg(relative, child.fileName());

    if (child.isSymbolicLink()) {
      // never followed: a link may point outside the folder being sent, or back into it
      entry.type = 'l';
      entries.push_back(entry);
    } else if (child.isDir()) {
      entry.type = 'd';
      entries.push_back(entry);
      if (!collectDirectory(entry.localPath, entry.relativePath, entries, total, maxEntries))
        return false;
    } else if (child.isFile()) {
      entry.size = static_cast<quint64>(child.size());
      total += entry.size;
      entries.push_back(entry);
    }
  }
  return true;
}

bool FileTransferManager::collectEntries(
    const QStringList &paths, std::vector<Entry> &entries, quint64 &total, QString &label, int &topLevelCount,
    size_t maxEntries
)
{
  QSet<QString> usedNames;
  bool complete = true;
  topLevelCount = 0;

  for (const auto &path : paths) {
    const QFileInfo info(QDir::cleanPath(path));
    if (!info.exists() || (!info.isDir() && !info.isFile()))
      continue;

    // two picked items may share a name if they come from different folders
    auto name = info.fileName();
    for (int i = 2; usedNames.contains(name); ++i)
      name = QStringLiteral("%1 (%2)").arg(info.fileName()).arg(i);
    usedNames.insert(name);

    if (topLevelCount++ == 0)
      label = name;

    Entry entry;
    entry.localPath = info.absoluteFilePath();
    entry.relativePath = name;
    if (info.isDir()) {
      entry.type = 'd';
      entries.push_back(entry);
      if (complete)
        complete = collectDirectory(entry.localPath, name, entries, total, maxEntries);
    } else {
      entry.size = static_cast<quint64>(info.size());
      total += entry.size;
      entries.push_back(entry);
    }
  }

  if (topLevelCount > 1)
    label = QStringLiteral("%1 (+%2)").arg(label).arg(topLevelCount - 1);
  return complete;
}

uint32_t FileTransferManager::sendFiles(const std::string &target, const QStringList &paths, Kind kind, uint32_t token)
{
  auto transfer = std::make_unique<Outgoing>();
  transfer->id = newId();
  transfer->peer = target;
  transfer->kind = kind;

  int count = 0;
  collectEntries(paths, transfer->entries, transfer->total, transfer->label, count, SIZE_MAX);
  if (transfer->entries.empty()) {
    LOG_WARN("file transfer: nothing to send to \"%s\"", target.c_str());
    return 0;
  }

  QJsonObject header;
  header["v"] = 1;
  header["kind"] = kindName(kind);
  header["source"] = QString::fromStdString(m_localName);
  header["target"] = QString::fromStdString(target);
  header["token"] = static_cast<qint64>(token);
  header["total"] = static_cast<qint64>(transfer->total);
  header["count"] = count;
  header["label"] = transfer->label;

  LOG_INFO(
      "file transfer %08x: sending %s to \"%s\", %llu bytes", transfer->id, qPrintable(transfer->label), target.c_str(),
      static_cast<unsigned long long>(transfer->total)
  );

  const auto id = transfer->id;
  if (!send(target, Type::Start, id, 0, toJson(header).toStdString())) {
    report(*transfer, State::Failed, QStringLiteral("computer is not connected"));
    return 0;
  }

  auto &ref = *transfer;
  m_outgoing[id] = std::move(transfer);
  report(ref, State::Active);
  pump(ref);
  return id;
}

bool FileTransferManager::fillBuffer(Outgoing &transfer, QString &error)
{
  while (transfer.buffer.size() < kChunkSize) {
    if (transfer.fileRemaining > 0) {
      const auto want = std::min<quint64>(kChunkSize - transfer.buffer.size(), transfer.fileRemaining);
      const auto offset = transfer.buffer.size();
      transfer.buffer.resize(offset + static_cast<qsizetype>(want));
      const auto got = transfer.file.read(transfer.buffer.data() + offset, static_cast<qint64>(want));
      if (got != static_cast<qint64>(want)) {
        error = QStringLiteral("%1 changed while it was being sent").arg(transfer.file.fileName());
        return false;
      }
      transfer.fileRemaining -= want;
      transfer.done += want;
      if (transfer.fileRemaining == 0)
        transfer.file.close();
      continue;
    }

    if (transfer.nextEntry >= transfer.entries.size()) {
      transfer.streamEnded = true;
      break;
    }

    const auto &entry = transfer.entries[transfer.nextEntry++];
    const QFileInfo info(entry.localPath);
    QJsonObject record;
    record["p"] = entry.relativePath;
    record["t"] = QString(QChar::fromLatin1(entry.type));

    if (entry.type == 'l') {
#if defined(Q_OS_UNIX)
      record["l"] = info.readSymLink();
#else
      continue;
#endif
    } else if (entry.type == 'f') {
      transfer.file.setFileName(entry.localPath);
      if (!transfer.file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot read %1: %2").arg(entry.localPath, transfer.file.errorString());
        return false;
      }
      record["s"] = static_cast<qint64>(entry.size);
      record["mt"] = info.lastModified().toSecsSinceEpoch();
      record["x"] = info.isExecutable();
      transfer.fileRemaining = entry.size;
      if (entry.size == 0)
        transfer.file.close();
    }

    const auto json = toJson(record);
    char length[4];
    qToBigEndian<quint32>(static_cast<quint32>(json.size()), length);
    transfer.buffer.append(length, 4);
    transfer.buffer.append(json);
  }
  return true;
}

bool FileTransferManager::pump(Outgoing &transfer)
{
  const auto id = transfer.id;
  while (!transfer.endSent && transfer.sent - transfer.acked < kWindow) {
    if (transfer.buffer.isEmpty() && !transfer.streamEnded) {
      if (QString error; !fillBuffer(transfer, error)) {
        failOutgoing(id, State::Failed, error, true);
        return false;
      }
    }

    if (!transfer.buffer.isEmpty()) {
      if (!send(transfer.peer, Type::Data, id, 0, transfer.buffer.toStdString())) {
        failOutgoing(id, State::Failed, QStringLiteral("connection lost"), false);
        return false;
      }
      ++transfer.sent;
      transfer.buffer.clear();
    } else if (transfer.streamEnded) {
      sendEnd(transfer.peer, id, FileTransferStatus::Ok);
      transfer.endSent = true;
    }
  }

  if (!transfer.lastReport.isValid() || transfer.lastReport.elapsed() >= kReportIntervalMs)
    report(transfer, State::Active);
  return true;
}

void FileTransferManager::failOutgoing(uint32_t id, State state, const QString &reason, bool notifyPeer)
{
  const auto it = m_outgoing.find(id);
  if (it == m_outgoing.end())
    return;

  const auto transfer = std::move(it->second);
  m_outgoing.erase(it);

  LOG_INFO("file transfer %08x: stopped sending: %s", id, qPrintable(reason));
  if (notifyPeer) {
    const auto status = state == State::Cancelled ? FileTransferStatus::Cancelled : FileTransferStatus::Error;
    sendEnd(transfer->peer, id, status, reason);
  }
  report(*transfer, state, reason);
}

//
// receiving
//

void FileTransferManager::handleStart(const std::string &from, const FileTransferMessage &message)
{
  const auto id = message.id;
  auto header = fromJson(message.payload);
  const auto target = header["target"].toString().toStdString();

  if (target != m_localName) {
    if (!m_isRouter) {
      sendEnd(from, id, FileTransferStatus::Error, QStringLiteral("delivered to the wrong computer"));
      return;
    }

    // only the server knows who a message really came from, so say so for the receiver
    header["source"] = QString::fromStdString(from);
    if (id == 0 || m_routes.contains(id) || m_incoming.contains(id) || m_outgoing.contains(id) ||
        !send(target, Type::Start, id, 0, toJson(header).toStdString())) {
      sendEnd(from, id, FileTransferStatus::Error, QStringLiteral("computer is not connected"));
      return;
    }

    LOG_DEBUG("file transfer %08x: relaying from \"%s\" to \"%s\"", id, from.c_str(), target.c_str());
    m_routes[id] = Route{from, target};
    return;
  }

  if (id == 0 || m_incoming.contains(id) || m_outgoing.contains(id) || m_routes.contains(id)) {
    sendEnd(from, id, FileTransferStatus::Error, QStringLiteral("transfer id is already in use"));
    return;
  }

  auto transfer = std::make_unique<Incoming>();
  transfer->id = id;
  transfer->link = from;
  transfer->source = m_isRouter ? from : header["source"].toString().toStdString();
  transfer->kind = header["kind"].toString() == kindName(Kind::Clipboard) ? Kind::Clipboard : Kind::Send;
  transfer->token = static_cast<uint32_t>(header["token"].toInteger());
  transfer->total = static_cast<quint64>(std::max<qint64>(0, header["total"].toInteger()));
  transfer->label = header["label"].toString();

  QString baseDir;
  if (transfer->kind == Kind::Clipboard) {
    // only accept clipboard files we asked for
    const auto requested = m_requested.find(transfer->token);
    if (requested == m_requested.end() || requested->second != transfer->source) {
      sendEnd(from, id, FileTransferStatus::Error, QStringLiteral("clipboard files were not requested"));
      return;
    }
    m_requested.erase(requested);
    baseDir = m_cacheDir;
    transfer->stageDir = cacheDirFor(transfer->source, transfer->token);
    QDir(transfer->stageDir).removeRecursively();
  } else {
    baseDir = m_downloadDir;
    transfer->stageDir = QStringLiteral("%1/%2%3").arg(baseDir, kStagePrefix).arg(id, 8, 16, QChar('0'));
  }

  QString error;
  if (baseDir.isEmpty() || !QDir().mkpath(transfer->stageDir)) {
    error = QStringLiteral("cannot create folder %1").arg(transfer->stageDir);
  } else if (const QStorageInfo storage(transfer->stageDir);
             storage.isValid() && storage.bytesAvailable() >= 0 &&
             static_cast<quint64>(storage.bytesAvailable()) < transfer->total + kFreeSpaceMargin) {
    error = QStringLiteral("not enough free disk space");
  }

  if (!error.isEmpty()) {
    LOG_WARN("file transfer %08x: refused: %s", id, qPrintable(error));
    QDir(transfer->stageDir).removeRecursively();
    sendEnd(from, id, FileTransferStatus::Error, error);
    report(*transfer, State::Failed, error);
    return;
  }

  LOG_INFO(
      "file transfer %08x: receiving %s from \"%s\", %llu bytes", id, qPrintable(transfer->label),
      transfer->source.c_str(), static_cast<unsigned long long>(transfer->total)
  );
  auto &ref = *transfer;
  m_incoming[id] = std::move(transfer);
  report(ref, State::Active);
}

void FileTransferManager::finishFile(Incoming &transfer)
{
  if (transfer.fileModified > 0)
    transfer.file.setFileTime(QDateTime::fromSecsSinceEpoch(transfer.fileModified), QFileDevice::FileModificationTime);
  transfer.file.close();
  if (transfer.fileExecutable) {
    transfer.file.setPermissions(
        transfer.file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther
    );
  }
}

bool FileTransferManager::beginRecord(Incoming &transfer, const QJsonObject &record, QString &error)
{
  const auto relative = sanitizeRelativePath(record["p"].toString());
  const auto type = record["t"].toString();
  if (relative.isEmpty() || type.size() != 1) {
    error = QStringLiteral("invalid file name in transfer");
    return false;
  }

  const auto path = QStringLiteral("%1/%2").arg(transfer.stageDir, relative);
  const auto parent = QFileInfo(path).absolutePath();
  const auto stage = QFileInfo(transfer.stageDir).canonicalFilePath();

  // a link received earlier must not redirect later entries out of our folder
  const auto canonicalParent = QDir().mkpath(parent) ? QFileInfo(parent).canonicalFilePath() : QString();
  if (stage.isEmpty() || (canonicalParent != stage && !canonicalParent.startsWith(stage + '/'))) {
    error = QStringLiteral("invalid folder in transfer");
    return false;
  }

  if (const auto top = relative.section('/', 0, 0); !transfer.topLevel.contains(top))
    transfer.topLevel.append(top);

  switch (type.at(0).toLatin1()) {
  case 'd':
    if (!QDir().mkpath(path)) {
      error = QStringLiteral("cannot create folder %1").arg(path);
      return false;
    }
    return true;

  case 'l':
#if defined(Q_OS_UNIX)
    QFile::remove(path);
    if (!QFile::link(record["l"].toString(), path))
      LOG_DEBUG("file transfer %08x: could not create link %s", transfer.id, qPrintable(path));
#endif
    return true;

  case 'f': {
    const auto size = static_cast<quint64>(std::max<qint64>(0, record["s"].toInteger()));
    if (size > transfer.total - transfer.done) {
      error = QStringLiteral("more data than announced");
      return false;
    }

    // never write through a link the sender planted at this name
    if (QFileInfo(path).isSymbolicLink())
      QFile::remove(path);

    transfer.file.setFileName(path);
    if (!transfer.file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      error = QStringLiteral("cannot write %1: %2").arg(path, transfer.file.errorString());
      return false;
    }
    transfer.fileRemaining = size;
    transfer.fileModified = record["mt"].toInteger();
    transfer.fileExecutable = record["x"].toBool();
    if (size == 0)
      finishFile(transfer);
    return true;
  }

  default:
    error = QStringLiteral("unknown entry type in transfer");
    return false;
  }
}

bool FileTransferManager::consume(Incoming &transfer, const std::string &data, QString &error)
{
  size_t pos = 0;
  while (pos < data.size()) {
    const auto left = data.size() - pos;

    if (transfer.fileRemaining > 0) {
      const auto count = static_cast<qint64>(std::min<quint64>(left, transfer.fileRemaining));
      if (transfer.file.write(data.data() + pos, count) != count) {
        error = QStringLiteral("cannot write %1: %2").arg(transfer.file.fileName(), transfer.file.errorString());
        return false;
      }
      pos += static_cast<size_t>(count);
      transfer.fileRemaining -= static_cast<quint64>(count);
      transfer.done += static_cast<quint64>(count);
      if (transfer.fileRemaining == 0)
        finishFile(transfer);
      continue;
    }

    // a record header: 4 byte length, then that much JSON
    qsizetype wanted = 4;
    if (transfer.pending.size() >= 4) {
      const auto length = qFromBigEndian<quint32>(transfer.pending.constData());
      if (length > kMaxRecordHeader) {
        error = QStringLiteral("corrupt transfer stream");
        return false;
      }
      wanted = 4 + static_cast<qsizetype>(length);
    }

    const auto take = std::min<size_t>(left, static_cast<size_t>(wanted - transfer.pending.size()));
    transfer.pending.append(data.data() + pos, static_cast<qsizetype>(take));
    pos += take;

    if (transfer.pending.size() >= 4 &&
        transfer.pending.size() == 4 + static_cast<qsizetype>(qFromBigEndian<quint32>(transfer.pending.constData()))) {
      const auto record = QJsonDocument::fromJson(transfer.pending.mid(4)).object();
      transfer.pending.clear();
      if (!beginRecord(transfer, record, error))
        return false;
    }
  }
  return true;
}

void FileTransferManager::handleData(const std::string &from, const FileTransferMessage &message)
{
  if (relay(from, message))
    return;

  const auto it = m_incoming.find(message.id);
  if (it == m_incoming.end() || it->second->link != from)
    return; // stale data for a transfer we already gave up on

  auto &transfer = *it->second;
  if (QString error; !consume(transfer, message.payload, error)) {
    failIncoming(message.id, State::Failed, error, true);
    return;
  }

  if (++transfer.received % kAckEvery == 0)
    send(transfer.link, Type::Ack, transfer.id, transfer.received, {});

  if (transfer.lastReport.elapsed() >= kReportIntervalMs)
    report(transfer, State::Active);
}

bool FileTransferManager::finalize(Incoming &transfer, QString &detail, QStringList &paths)
{
  if (transfer.fileRemaining > 0 || !transfer.pending.isEmpty() || transfer.done != transfer.total) {
    detail = QStringLiteral("transfer ended early");
    return false;
  }

  if (transfer.kind == Kind::Clipboard) {
    for (const auto &name : std::as_const(transfer.topLevel))
      paths.append(QStringLiteral("%1/%2").arg(transfer.stageDir, name));
    detail = transfer.stageDir;
    return true;
  }

  for (const auto &name : std::as_const(transfer.topLevel)) {
    const auto destination = uniquePath(m_downloadDir, name);
    if (!QDir().rename(QStringLiteral("%1/%2").arg(transfer.stageDir, name), destination)) {
      detail = QStringLiteral("cannot move %1 into %2").arg(name, m_downloadDir);
      return false;
    }
    paths.append(destination);
  }
  QDir(transfer.stageDir).removeRecursively();
  detail = paths.size() == 1 ? paths.first() : m_downloadDir;
  return true;
}

void FileTransferManager::failIncoming(uint32_t id, State state, const QString &reason, bool notifyPeer)
{
  const auto it = m_incoming.find(id);
  if (it == m_incoming.end())
    return;

  const auto transfer = std::move(it->second);
  m_incoming.erase(it);
  transfer->file.close();
  QDir(transfer->stageDir).removeRecursively();

  LOG_INFO("file transfer %08x: stopped receiving: %s", id, qPrintable(reason));
  if (notifyPeer) {
    const auto status = state == State::Cancelled ? FileTransferStatus::Cancelled : FileTransferStatus::Error;
    sendEnd(transfer->link, id, status, reason);
  }
  report(*transfer, state, reason);
}

void FileTransferManager::handleAck(const std::string &from, const FileTransferMessage &message)
{
  if (relay(from, message))
    return;

  const auto it = m_outgoing.find(message.id);
  if (it == m_outgoing.end())
    return;

  auto &transfer = *it->second;
  // never trust the peer to move the window backwards or past what was sent
  transfer.acked = std::clamp(message.value, transfer.acked, transfer.sent);
  pump(transfer);
}

void FileTransferManager::handleEnd(const std::string &from, const FileTransferMessage &message)
{
  if (relay(from, message))
    return;

  const auto id = message.id;
  const auto status = static_cast<FileTransferStatus>(message.value);
  const auto reason = QString::fromStdString(message.payload);

  if (const auto it = m_outgoing.find(id); it != m_outgoing.end()) {
    // the receiver's verdict: stored, cancelled or failed
    if (status == FileTransferStatus::Ok && it->second->endSent) {
      const auto transfer = std::move(it->second);
      m_outgoing.erase(it);
      LOG_INFO("file transfer %08x: sent", id);
      transfer->done = transfer->total;
      report(*transfer, State::Completed, reason);
    } else {
      const auto state = status == FileTransferStatus::Ok ? State::Failed : stateFor(status);
      failOutgoing(id, state, reason, false);
    }
    return;
  }

  const auto it = m_incoming.find(id);
  if (it == m_incoming.end() || it->second->link != from)
    return;

  if (status != FileTransferStatus::Ok) {
    failIncoming(id, stateFor(status), reason, false);
    return;
  }

  auto &ref = *it->second;
  QString detail;
  QStringList paths;
  if (!finalize(ref, detail, paths)) {
    failIncoming(id, State::Failed, detail, true);
    return;
  }

  const auto transfer = std::move(it->second);
  m_incoming.erase(it);
  LOG_INFO("file transfer %08x: received into %s", id, qPrintable(detail));
  sendEnd(transfer->link, id, FileTransferStatus::Ok);
  report(*transfer, State::Completed, detail);

  if (transfer->kind == Kind::Clipboard) {
    m_fetched[fetchedKey(transfer->source, transfer->token)] = paths;
    if (m_clipboardFiles)
      m_clipboardFiles(transfer->source, transfer->token, paths);
  }
}

//
// relaying
//

bool FileTransferManager::relay(const std::string &from, const FileTransferMessage &message)
{
  if (!m_isRouter)
    return false;

  const auto it = m_routes.find(message.id);
  if (it == m_routes.end())
    return false;

  const auto route = it->second;
  const bool fromSource = from == route.source;
  if (!fromSource && from != route.target)
    return true; // not a party to this transfer

  const bool isEnd = message.type == Type::End;
  // the transfer is over once the receiver answers, or the sender gives up
  if (isEnd && (!fromSource || message.value != static_cast<uint32_t>(FileTransferStatus::Ok)))
    m_routes.erase(it);

  const auto &destination = fromSource ? route.target : route.source;
  if (!m_send(destination, message) && !isEnd) {
    m_routes.erase(message.id);
    sendEnd(from, message.id, FileTransferStatus::Error, QStringLiteral("computer is not connected"));
  }
  return true;
}

void FileTransferManager::handleMessage(const std::string &from, const FileTransferMessage &message)
{
  switch (message.type) {
  case Type::Start:
    handleStart(from, message);
    break;
  case Type::Data:
    handleData(from, message);
    break;
  case Type::Ack:
    handleAck(from, message);
    break;
  case Type::End:
    handleEnd(from, message);
    break;
  case Type::Request:
    handleRequest(from, message);
    break;
  case Type::Decline:
    handleDecline(message);
    break;
  }
}

void FileTransferManager::cancel(uint32_t id)
{
  const auto reason = QStringLiteral("cancelled");
  failOutgoing(id, State::Cancelled, reason, true);
  failIncoming(id, State::Cancelled, reason, true);
}

void FileTransferManager::peerDisconnected(const std::string &peer)
{
  const auto reason = QStringLiteral("computer disconnected");

  std::vector<uint32_t> ids;
  for (const auto &[id, transfer] : m_outgoing) {
    if (transfer->peer == peer)
      ids.push_back(id);
  }
  for (const auto id : ids)
    failOutgoing(id, State::Failed, reason, false);

  ids.clear();
  for (const auto &[id, transfer] : m_incoming) {
    if (transfer->link == peer)
      ids.push_back(id);
  }
  for (const auto id : ids)
    failIncoming(id, State::Failed, reason, false);

  for (auto it = m_routes.begin(); it != m_routes.end();) {
    if (it->second.source == peer) {
      sendEnd(it->second.target, it->first, FileTransferStatus::Error, reason);
      it = m_routes.erase(it);
    } else if (it->second.target == peer) {
      sendEnd(it->second.source, it->first, FileTransferStatus::Error, reason);
      it = m_routes.erase(it);
    } else {
      ++it;
    }
  }

  std::erase_if(m_requested, [&peer](const auto &item) { return item.second == peer; });
}

void FileTransferManager::abortAll(const QString &reason)
{
  while (!m_outgoing.empty())
    failOutgoing(m_outgoing.begin()->first, State::Failed, reason, false);
  while (!m_incoming.empty())
    failIncoming(m_incoming.begin()->first, State::Failed, reason, false);
  m_routes.clear();
  m_requested.clear();
}

//
// clipboard offers
//

QString FileTransferManager::cacheDirFor(const std::string &origin, uint32_t token) const
{
  auto name = sanitizeRelativePath(QString::fromStdString(origin).replace('/', '_'));
  if (name.isEmpty())
    name = QStringLiteral("computer");
  return QStringLiteral("%1/%2-%3").arg(m_cacheDir, name).arg(token, 8, 16, QChar('0'));
}

std::string FileTransferManager::createOffer(const QStringList &paths)
{
  // re-reading an unchanged clipboard must produce the same offer,
  // or every read would look like a new clipboard to the other computers
  if (paths == m_offerPaths && !m_offerJson.empty())
    return m_offerJson;

  std::vector<Entry> entries;
  quint64 total = 0;
  QString label;
  int count = 0;
  const bool measured = collectEntries(paths, entries, total, label, count, kMaxOfferEntries);
  if (count == 0) {
    m_offerPaths.clear();
    m_offerJson.clear();
    return {};
  }

  m_offerToken += 2;
  m_offerPaths = paths;

  QJsonObject offer;
  offer["origin"] = QString::fromStdString(m_localName);
  offer["token"] = static_cast<qint64>(m_offerToken);
  offer["total"] = static_cast<qint64>(measured ? total : kUnknownSize);
  offer["count"] = count;
  offer["label"] = label;
  m_offerJson = toJson(offer).toStdString();
  return m_offerJson;
}

FileTransferManager::Offer FileTransferManager::parseOffer(const std::string &json)
{
  Offer offer;
  const auto object = fromJson(json);
  offer.origin = object["origin"].toString().toStdString();
  offer.token = static_cast<uint32_t>(object["token"].toInteger());
  offer.total = static_cast<quint64>(std::max<qint64>(0, object["total"].toInteger()));
  offer.count = object["count"].toInt();
  offer.label = object["label"].toString();
  offer.valid = !offer.origin.empty() && offer.token != 0 && offer.count > 0;
  return offer;
}

QStringList FileTransferManager::localPathsForOffer(const Offer &offer) const
{
  if (!offer.valid)
    return {};

  if (offer.origin == m_localName)
    return offer.token == m_offerToken ? m_offerPaths : QStringList();

  const auto it = m_fetched.find(fetchedKey(offer.origin, offer.token));
  if (it == m_fetched.end())
    return {};

  // the cache may have been cleaned behind our back
  for (const auto &path : it->second) {
    if (!QFileInfo::exists(path))
      return {};
  }
  return it->second;
}

bool FileTransferManager::requestOffer(const Offer &offer)
{
  if (!offer.valid || offer.origin == m_localName || m_cacheDir.isEmpty())
    return false;

  if (const auto it = m_requested.find(offer.token); it != m_requested.end() && it->second == offer.origin)
    return true; // already on its way

  QJsonObject request;
  request["origin"] = QString::fromStdString(offer.origin);
  request["requester"] = QString::fromStdString(m_localName);
  if (!send(offer.origin, Type::Request, offer.token, 0, toJson(request).toStdString()))
    return false;

  m_requested[offer.token] = offer.origin;
  return true;
}

void FileTransferManager::handleRequest(const std::string &from, const FileTransferMessage &message)
{
  auto request = fromJson(message.payload);
  const auto origin = request["origin"].toString().toStdString();
  // on the server the connection says who is asking; a client has to believe the server
  const auto requester = m_isRouter ? from : request["requester"].toString().toStdString();

  if (origin != m_localName) {
    if (!m_isRouter)
      return;

    request["requester"] = QString::fromStdString(from);
    if (!send(origin, Type::Request, message.id, 0, toJson(request).toStdString()))
      send(from, Type::Decline, message.id, 0, message.payload);
    return;
  }

  if (requester.empty() || requester == m_localName)
    return;

  if (message.id != m_offerToken || m_offerPaths.isEmpty() ||
      sendFiles(requester, m_offerPaths, Kind::Clipboard, message.id) == 0) {
    QJsonObject decline;
    decline["origin"] = QString::fromStdString(m_localName);
    decline["requester"] = QString::fromStdString(requester);
    send(requester, Type::Decline, message.id, 0, toJson(decline).toStdString());
  }
}

void FileTransferManager::handleDecline(const FileTransferMessage &message)
{
  const auto decline = fromJson(message.payload);
  const auto requester = decline["requester"].toString().toStdString();

  if (requester != m_localName) {
    if (m_isRouter)
      m_send(requester, message);
    return;
  }

  LOG_DEBUG("file transfer: clipboard files %08x are no longer offered", message.id);
  m_requested.erase(message.id);
}

//
// progress
//

void FileTransferManager::report(Outgoing &transfer, State state, const QString &detail)
{
  transfer.lastReport.start();
  if (!m_progress)
    return;

  Progress progress;
  progress.id = transfer.id;
  progress.incoming = false;
  progress.kind = transfer.kind;
  progress.state = state;
  progress.peer = QString::fromStdString(transfer.peer);
  progress.label = transfer.label;
  progress.total = transfer.total;
  progress.done = transfer.done;
  progress.detail = detail;
  m_progress(progress);
}

void FileTransferManager::report(Incoming &transfer, State state, const QString &detail)
{
  transfer.lastReport.start();
  if (!m_progress)
    return;

  Progress progress;
  progress.id = transfer.id;
  progress.incoming = true;
  progress.kind = transfer.kind;
  progress.state = state;
  progress.peer = QString::fromStdString(transfer.source);
  progress.label = transfer.label;
  progress.total = transfer.total;
  progress.done = transfer.done;
  progress.detail = detail;
  m_progress(progress);
}

} // namespace deskflow
