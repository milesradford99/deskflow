/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace deskflow {

//! One file transfer protocol message, independent of how it is framed on the wire
struct FileTransferMessage
{
  enum class Type : uint8_t
  {
    Start,   //!< sender -> receiver: JSON header, opens transfer \c id
    Data,    //!< sender -> receiver: a slice of the record stream
    Ack,     //!< receiver -> sender: \c value data messages consumed so far
    End,     //!< either way: \c value is a FileTransferStatus, payload a reason
    Request, //!< ask the origin of a clipboard file offer (\c id is the token) to send it
    Decline  //!< origin -> requester: offer \c id is no longer available
  };

  Type type = Type::Start;
  uint32_t id = 0;
  uint32_t value = 0;
  std::string payload;
};

enum class FileTransferStatus : uint8_t
{
  Ok = 0,
  Cancelled = 1,
  Error = 2
};

//! Sends, receives and relays files between computers
/*!
A transfer is a stream of records, each a 4 byte big endian length, that many
bytes of JSON describing one file, folder or link, and then the file's bytes.
The stream is cut into Data messages; the receiver acknowledges them and the
sender keeps a bounded number unacknowledged. That keeps memory flat for any
file size and leaves room on the shared connection for keyboard and mouse.

Clients only talk to the server, so the server's manager also relays: a
transfer addressed to another computer is forwarded there message by message.

All methods must be called from the same thread. Nothing here blocks on the
network; disk access is done in chunk sized pieces.
*/
class FileTransferManager
{
public:
  enum class Kind
  {
    Send,     //!< Files the user sent on purpose; stored in the download folder
    Clipboard //!< Files behind a copied clipboard; stored in the cache folder
  };

  enum class State
  {
    Active,
    Completed,
    Cancelled,
    Failed
  };

  //! Snapshot of a transfer, reported whenever it advances
  struct Progress
  {
    uint32_t id = 0;
    bool incoming = false;
    Kind kind = Kind::Send;
    State state = State::Active;
    QString peer;      //!< the computer at the other end
    QString label;     //!< "report.pdf" or "report.pdf and 3 more"
    quint64 total = 0; //!< bytes
    quint64 done = 0;  //!< bytes
    QString detail;    //!< failure reason, or where a completed download was stored
  };

  //! Returns false if the message could not be queued for \c peer
  using SendFunc = std::function<bool(const std::string &peer, const FileTransferMessage &)>;
  using ProgressFunc = std::function<void(const Progress &)>;
  //! Called when the files behind clipboard offer \c token of \c origin are available locally
  using ClipboardFilesFunc = std::function<void(const std::string &origin, uint32_t token, const QStringList &paths)>;

  /*!
  \c localName is this computer's name. \c isRouter is true on the server.
  On a client every message goes to the server, whatever \c peer says.
  */
  FileTransferManager(std::string localName, bool isRouter, SendFunc send);
  ~FileTransferManager();

  FileTransferManager(const FileTransferManager &) = delete;
  FileTransferManager &operator=(const FileTransferManager &) = delete;

  void setProgressHandler(ProgressFunc handler);
  void setClipboardFilesHandler(ClipboardFilesFunc handler);

  //! Folder that received files are moved into. Created on demand.
  void setDownloadDir(const QString &dir);
  //! Folder holding files fetched for the clipboard. Emptied by clearCache().
  void setCacheDir(const QString &dir);
  //! Remove everything fetched for the clipboard, e.g. at startup.
  void clearCache();

  const std::string &localName() const
  {
    return m_localName;
  }

  //! Send files and folders to \c target. Returns the transfer id, or 0 if nothing could be sent.
  uint32_t sendFiles(const std::string &target, const QStringList &paths, Kind kind = Kind::Send, uint32_t token = 0);

  //! Cancel a transfer in either direction.
  void cancel(uint32_t id);

  //! Feed a message that arrived from the directly connected \c from.
  void handleMessage(const std::string &from, const FileTransferMessage &message);

  //! Abort everything that involves a peer that went away.
  void peerDisconnected(const std::string &peer);

  //! Abort every transfer, e.g. when a client loses its server.
  void abortAll(const QString &reason);

  //! Number of transfers this manager is sending, receiving or relaying.
  size_t activeCount() const;

  //! @name Clipboard offers
  //@{

  //! Remember \c paths as the files behind the local clipboard; returns the offer to put on the wire.
  std::string createOffer(const QStringList &paths);

  struct Offer
  {
    bool valid = false;
    std::string origin;
    uint32_t token = 0;
    quint64 total = 0;
    int count = 0;
    QString label;
    std::string json; //!< the offer as it travels in the clipboard
  };
  static Offer parseOffer(const std::string &json);

  //! The offer to put on the wire for local clipboard \c paths.
  /*!
  Files we fetched for the clipboard keep the offer of the computer they came
  from, so a clipboard that bounces back doesn't turn into a new, second-hand copy.
  */
  std::string offerForLocalPaths(const QStringList &paths);

  //! Local paths for an offer if it is ours or was already fetched; empty otherwise.
  QStringList localPathsForOffer(const Offer &offer) const;

  //! Ask the origin to send the files behind \c offer. The clipboard files handler fires when done.
  bool requestOffer(const Offer &offer);
  //@}

  static constexpr int kChunkSize = 64 * 1024; //!< bytes of stream per Data message
  static constexpr uint32_t kWindow = 16;      //!< most Data messages in flight
  static constexpr uint32_t kAckEvery = 4;     //!< receiver acknowledges every N Data messages
  static constexpr quint32 kMaxRecordHeader = 64 * 1024;
  //! Offers stop measuring a folder after this many entries and report an unknown (huge) size.
  static constexpr size_t kMaxOfferEntries = 20000;
  static constexpr quint64 kUnknownSize = Q_UINT64_C(1) << 52;

private:
  struct Entry
  {
    QString localPath;
    QString relativePath;
    char type = 'f'; // f: file, d: directory, l: symbolic link
    quint64 size = 0;
  };

  struct Outgoing
  {
    uint32_t id = 0;
    std::string peer;
    Kind kind = Kind::Send;
    QString label;
    std::vector<Entry> entries;
    size_t nextEntry = 0;
    QFile file;
    quint64 fileRemaining = 0;
    QByteArray buffer;
    quint64 total = 0;
    quint64 done = 0;
    uint32_t sent = 0;
    uint32_t acked = 0;
    bool streamEnded = false;
    bool endSent = false;
    QElapsedTimer lastReport;
  };

  struct Incoming
  {
    uint32_t id = 0;
    std::string link;   // who to answer
    std::string source; // who the files come from
    Kind kind = Kind::Send;
    uint32_t token = 0;
    std::string offerJson;
    QString label;
    QString stageDir;
    QStringList topLevel;
    QByteArray pending;
    QFile file;
    quint64 fileRemaining = 0;
    qint64 fileModified = 0;
    bool fileExecutable = false;
    quint64 total = 0;
    quint64 done = 0;
    uint32_t received = 0;
    QElapsedTimer lastReport;
  };

  struct Route
  {
    std::string source;
    std::string target;
  };

  bool send(const std::string &peer, FileTransferMessage::Type type, uint32_t id, uint32_t value, std::string payload);
  void sendEnd(const std::string &peer, uint32_t id, FileTransferStatus status, const QString &reason = {});

  void handleStart(const std::string &from, const FileTransferMessage &message);
  void handleData(const std::string &from, const FileTransferMessage &message);
  void handleAck(const std::string &from, const FileTransferMessage &message);
  void handleEnd(const std::string &from, const FileTransferMessage &message);
  void handleRequest(const std::string &from, const FileTransferMessage &message);
  void handleDecline(const FileTransferMessage &message);
  bool relay(const std::string &from, const FileTransferMessage &message);

  //! Walks \c paths; returns false if \c maxEntries was hit before the walk finished.
  static bool collectEntries(
      const QStringList &paths, std::vector<Entry> &entries, quint64 &total, QString &label, int &topLevelCount,
      size_t maxEntries
  );
  static bool collectDirectory(
      const QString &dir, const QString &relative, std::vector<Entry> &entries, quint64 &total, size_t maxEntries
  );
  bool pump(Outgoing &transfer);
  bool fillBuffer(Outgoing &transfer, QString &error);
  void failOutgoing(uint32_t id, State state, const QString &reason, bool notifyPeer);

  bool consume(Incoming &transfer, const std::string &data, QString &error);
  bool beginRecord(Incoming &transfer, const QJsonObject &record, QString &error);
  void finishFile(Incoming &transfer);
  bool finalize(Incoming &transfer, QString &detail, QStringList &paths);
  void failIncoming(uint32_t id, State state, const QString &reason, bool notifyPeer);

  void report(Outgoing &transfer, State state, const QString &detail = {});
  void report(Incoming &transfer, State state, const QString &detail = {});

  QString cacheDirFor(const std::string &origin, uint32_t token) const;
  uint32_t newId() const;

  std::string m_localName;
  bool m_isRouter;
  SendFunc m_send;
  ProgressFunc m_progress;
  ClipboardFilesFunc m_clipboardFiles;
  QString m_downloadDir;
  QString m_cacheDir;

  std::map<uint32_t, std::unique_ptr<Outgoing>> m_outgoing;
  std::map<uint32_t, std::unique_ptr<Incoming>> m_incoming;
  std::map<uint32_t, Route> m_routes;

  // the newest local clipboard offer; older tokens stop being served
  uint32_t m_offerToken = 0;
  QStringList m_offerPaths;
  std::string m_offerJson;
  // clipboard files already fetched: "origin/token" -> local paths
  struct Fetched
  {
    QStringList paths;
    std::string json;
  };
  std::map<std::string, Fetched> m_fetched;
  // offers we asked for and have not received yet, by token
  std::map<uint32_t, Offer> m_requested;
};

} // namespace deskflow
