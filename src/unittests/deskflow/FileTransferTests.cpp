/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferTests.h"

#include "deskflow/FileTransfer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <deque>

using deskflow::FileTransferManager;
using deskflow::FileTransferMessage;
using deskflow::FileTransferStatus;
using State = FileTransferManager::State;
using Type = FileTransferMessage::Type;

namespace {

//! A server and two clients joined by in-memory links that deliver messages in order.
class Network
{
public:
  struct Node
  {
    std::unique_ptr<FileTransferManager> manager;
    QString downloads;
    QString cache;
    std::vector<FileTransferManager::Progress> reports;
    QStringList clipboardPaths;
    bool online = true;

    State lastState() const
    {
      return reports.empty() ? State::Active : reports.back().state;
    }
  };

  struct Packet
  {
    std::string from;
    std::string to;
    FileTransferMessage message;
  };

  Network()
  {
    addNode("server", true);
    addNode("alice", false);
    addNode("bob", false);
  }

  Node &node(const std::string &name)
  {
    return m_nodes.at(name);
  }

  FileTransferManager &manager(const std::string &name)
  {
    return *m_nodes.at(name).manager;
  }

  //! Deliver queued messages until the network is quiet; returns how many were delivered.
  int run(int limit = 1000000)
  {
    int delivered = 0;
    while (!m_queue.empty() && delivered < limit) {
      auto packet = std::move(m_queue.front());
      m_queue.pop_front();
      m_maxQueued = std::max(m_maxQueued, m_queue.size() + 1);
      if (!m_nodes.at(packet.to).online)
        continue;
      if (filter && !filter(packet))
        continue;
      m_nodes.at(packet.to).manager->handleMessage(packet.from, packet.message);
      ++delivered;
    }
    return delivered;
  }

  size_t maxQueued() const
  {
    return m_maxQueued;
  }

  QString makeFile(const QString &relative, const QByteArray &content)
  {
    const auto path = m_dir.filePath("source/" + relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
      file.write(content);
    return path;
  }

  QString sourcePath(const QString &relative) const
  {
    return m_dir.filePath("source/" + relative);
  }

  //! Return false to drop a packet, or edit it in place.
  std::function<bool(Packet &)> filter;

private:
  void addNode(const std::string &name, bool isRouter)
  {
    auto &node = m_nodes[name];
    node.downloads = m_dir.filePath(QString::fromStdString(name) + "/downloads");
    node.cache = m_dir.filePath(QString::fromStdString(name) + "/cache");
    node.manager = std::make_unique<FileTransferManager>(
        name, isRouter,
        [this, name, isRouter](const std::string &peer, const FileTransferMessage &message) {
          // a client's only link is the server
          const auto to = isRouter ? peer : std::string("server");
          if (!m_nodes.contains(to) || !m_nodes.at(to).online)
            return false;
          m_queue.push_back({name, to, message});
          return true;
        }
    );
    node.manager->setDownloadDir(node.downloads);
    node.manager->setCacheDir(node.cache);
    node.manager->setProgressHandler([&node](const auto &progress) { node.reports.push_back(progress); });
    node.manager->setClipboardFilesHandler([&node](const auto &, uint32_t, const QStringList &paths) {
      node.clipboardPaths = paths;
    });
  }

  QTemporaryDir m_dir;
  std::map<std::string, Node> m_nodes;
  std::deque<Packet> m_queue;
  size_t m_maxQueued = 0;
};

QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
}

QByteArray pattern(int size)
{
  QByteArray data(size, Qt::Uninitialized);
  for (int i = 0; i < size; ++i)
    data[i] = static_cast<char>((i * 31 + (i >> 8)) & 0xff);
  return data;
}

std::string record(const QJsonObject &object)
{
  const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
  std::string out(4, '\0');
  qToBigEndian<quint32>(static_cast<quint32>(json.size()), out.data());
  return out + json.toStdString();
}

std::string startHeader(const QString &kind, qint64 total, qint64 token = 0)
{
  QJsonObject header{{"v", 1},         {"kind", kind},   {"source", "alice"}, {"target", "server"},
                     {"token", token}, {"total", total}, {"count", 1},        {"label", "x"}};
  return QJsonDocument(header).toJson(QJsonDocument::Compact).toStdString();
}

QStringList visibleEntries(const QString &dir)
{
  return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

} // namespace

void FileTransferTests::clientToServer_storesFilesInDownloadDir()
{
  Network net;
  const auto file = net.makeFile("notes.txt", "hello from alice");

  const auto id = net.manager("alice").sendFiles("server", {file});
  QVERIFY(id != 0);
  net.run();

  QCOMPARE(readFile(net.node("server").downloads + "/notes.txt"), QByteArray("hello from alice"));
  QCOMPARE(visibleEntries(net.node("server").downloads), QStringList{"notes.txt"});
  QCOMPARE(net.node("alice").lastState(), State::Completed);
  QCOMPARE(net.node("server").lastState(), State::Completed);
  QCOMPARE(net.node("server").reports.back().peer, QString("alice"));
  QCOMPARE(net.node("server").reports.back().detail, net.node("server").downloads + "/notes.txt");
  QCOMPARE(net.manager("alice").activeCount(), size_t(0));
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
}

void FileTransferTests::serverToClient_keepsFolderStructure()
{
  Network net;
  net.makeFile("project/readme.md", "# readme");
  net.makeFile("project/src/main.cpp", "int main() {}");
  net.makeFile("project/src/empty.txt", "");
  QDir().mkpath(net.sourcePath("project/assets/blank"));
  const auto single = net.makeFile("single.bin", pattern(1000));

  net.manager("server").sendFiles("bob", {net.sourcePath("project"), single});
  net.run();

  const auto downloads = net.node("bob").downloads;
  QCOMPARE(readFile(downloads + "/project/readme.md"), QByteArray("# readme"));
  QCOMPARE(readFile(downloads + "/project/src/main.cpp"), QByteArray("int main() {}"));
  QCOMPARE(readFile(downloads + "/project/src/empty.txt"), QByteArray(""));
  QVERIFY(QFileInfo(downloads + "/project/assets/blank").isDir());
  QCOMPARE(readFile(downloads + "/single.bin"), pattern(1000));
  QCOMPARE(net.node("bob").lastState(), State::Completed);
  // several items: point the user at the folder rather than one of them
  QCOMPARE(net.node("bob").reports.back().detail, downloads);
  QCOMPARE(net.node("bob").reports.back().label, QString("project (+1)"));
}

void FileTransferTests::clientToClient_isRelayedByServer()
{
  Network net;
  const auto data = pattern(300 * 1024);
  const auto file = net.makeFile("big.bin", data);

  net.manager("alice").sendFiles("bob", {file});
  net.run();

  QCOMPARE(readFile(net.node("bob").downloads + "/big.bin"), data);
  QCOMPARE(net.node("alice").lastState(), State::Completed);
  QCOMPARE(net.node("bob").lastState(), State::Completed);
  QCOMPARE(net.node("bob").reports.back().peer, QString("alice"));
  // the server only forwards: nothing stored, nothing reported, nothing left behind
  QVERIFY(net.node("server").reports.empty());
  QVERIFY(!QFileInfo::exists(net.node("server").downloads));
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
}

void FileTransferTests::largeFile_staysInsideFlowControlWindow()
{
  Network net;
  const auto data = pattern(8 * 1024 * 1024);
  const auto file = net.makeFile("large.bin", data);

  net.manager("alice").sendFiles("server", {file});
  net.run();

  QCOMPARE(readFile(net.node("server").downloads + "/large.bin"), data);
  // start + a full window + acks in flight; far fewer than the 128+ chunks of the file
  QVERIFY2(net.maxQueued() <= FileTransferManager::kWindow + 8, qPrintable(QString::number(net.maxQueued())));
}

void FileTransferTests::existingName_getsNumberedCopy()
{
  Network net;
  const auto file = net.makeFile("report.pdf", "new");
  QDir().mkpath(net.node("server").downloads);
  QFile existing(net.node("server").downloads + "/report.pdf");
  QVERIFY(existing.open(QIODevice::WriteOnly));
  existing.write("old");
  existing.close();

  net.manager("alice").sendFiles("server", {file});
  net.run();

  QCOMPARE(readFile(net.node("server").downloads + "/report.pdf"), QByteArray("old"));
  QCOMPARE(readFile(net.node("server").downloads + "/report (2).pdf"), QByteArray("new"));
}

void FileTransferTests::emptySelection_sendsNothing()
{
  Network net;
  QCOMPARE(net.manager("alice").sendFiles("server", {net.sourcePath("does-not-exist")}), 0u);
  QCOMPARE(net.run(), 0);
}

void FileTransferTests::unknownTarget_failsSender()
{
  Network net;
  const auto file = net.makeFile("a.txt", "a");

  net.manager("alice").sendFiles("carol", {file});
  net.run();

  QCOMPARE(net.node("alice").lastState(), State::Failed);
  QCOMPARE(net.manager("alice").activeCount(), size_t(0));
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
}

void FileTransferTests::cancelBySender_removesPartialDownload()
{
  Network net;
  const auto file = net.makeFile("large.bin", pattern(4 * 1024 * 1024));

  const auto id = net.manager("alice").sendFiles("bob", {file});
  net.run(20);
  QVERIFY(!visibleEntries(net.node("bob").downloads).isEmpty());

  net.manager("alice").cancel(id);
  net.run();

  QCOMPARE(net.node("alice").lastState(), State::Cancelled);
  QCOMPARE(net.node("bob").lastState(), State::Cancelled);
  QVERIFY(visibleEntries(net.node("bob").downloads).isEmpty());
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
  QCOMPARE(net.manager("bob").activeCount(), size_t(0));
}

void FileTransferTests::cancelByReceiver_stopsSender()
{
  Network net;
  const auto file = net.makeFile("large.bin", pattern(4 * 1024 * 1024));

  const auto id = net.manager("server").sendFiles("alice", {file});
  net.run(20);
  net.manager("alice").cancel(id);
  net.run();

  QCOMPARE(net.node("server").lastState(), State::Cancelled);
  QCOMPARE(net.node("alice").lastState(), State::Cancelled);
  QVERIFY(visibleEntries(net.node("alice").downloads).isEmpty());
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
}

void FileTransferTests::pathTraversal_isRejected()
{
  Network net;
  auto &server = net.manager("server");

  server.handleMessage("alice", {Type::Start, 7, 0, startHeader("send", 4)});
  server.handleMessage("alice", {Type::Data, 7, 0, record({{"p", "../escaped.txt"}, {"t", "f"}, {"s", 4}}) + "evil"});
  net.run();

  QCOMPARE(net.node("server").lastState(), State::Failed);
  QVERIFY(!QFileInfo::exists(net.node("server").downloads + "/../escaped.txt"));
  QVERIFY(visibleEntries(net.node("server").downloads).isEmpty());
  QCOMPARE(server.activeCount(), size_t(0));
}

void FileTransferTests::truncatedStream_isRejected()
{
  Network net;
  auto &server = net.manager("server");

  server.handleMessage("alice", {Type::Start, 9, 0, startHeader("send", 10)});
  server.handleMessage("alice", {Type::Data, 9, 0, record({{"p", "short.txt"}, {"t", "f"}, {"s", 10}}) + "12345"});
  server.handleMessage("alice", {Type::End, 9, static_cast<uint32_t>(FileTransferStatus::Ok), {}});
  net.run();

  QCOMPARE(net.node("server").lastState(), State::Failed);
  QVERIFY(visibleEntries(net.node("server").downloads).isEmpty());
}

void FileTransferTests::receiverDisconnect_failsRelayedSender()
{
  Network net;
  const auto file = net.makeFile("large.bin", pattern(4 * 1024 * 1024));

  net.manager("alice").sendFiles("bob", {file});
  net.run(20);
  net.node("bob").online = false;
  net.manager("server").peerDisconnected("bob");
  net.run();

  QCOMPARE(net.node("alice").lastState(), State::Failed);
  QCOMPARE(net.manager("alice").activeCount(), size_t(0));
  QCOMPARE(net.manager("server").activeCount(), size_t(0));
}

void FileTransferTests::clipboardOffer_isStableForSamePaths()
{
  Network net;
  const auto a = net.makeFile("a.txt", "aaa");
  const auto b = net.makeFile("b.txt", "bbbbb");
  auto &alice = net.manager("alice");

  const auto first = alice.createOffer({a, b});
  QCOMPARE(alice.createOffer({a, b}), first);

  const auto offer = FileTransferManager::parseOffer(first);
  QVERIFY(offer.valid);
  QCOMPARE(offer.origin, std::string("alice"));
  QCOMPARE(offer.total, quint64(8));
  QCOMPARE(offer.count, 2);
  QCOMPARE(alice.localPathsForOffer(offer), (QStringList{a, b}));

  const auto second = FileTransferManager::parseOffer(alice.createOffer({a}));
  QVERIFY(second.token != offer.token);
  // the superseded offer no longer resolves, even on its own computer
  QVERIFY(alice.localPathsForOffer(offer).isEmpty());
  QVERIFY(alice.createOffer({}).empty());
}

void FileTransferTests::clipboardOffer_isFetchedThroughRelay()
{
  Network net;
  const auto file = net.makeFile("photo.png", pattern(100 * 1024));
  net.makeFile("album/one.jpg", "1");
  const auto offer = FileTransferManager::parseOffer(net.manager("alice").createOffer({file, net.sourcePath("album")}));

  auto &bob = net.manager("bob");
  QVERIFY(bob.localPathsForOffer(offer).isEmpty());
  QVERIFY(bob.requestOffer(offer));
  net.run();

  const auto paths = net.node("bob").clipboardPaths;
  QCOMPARE(paths.size(), 2);
  QVERIFY(paths.at(0).startsWith(net.node("bob").cache));
  QCOMPARE(readFile(paths.at(0)), pattern(100 * 1024));
  QCOMPARE(readFile(paths.at(1) + "/one.jpg"), QByteArray("1"));
  // nothing lands in the download folder, and the result is remembered
  QVERIFY(!QFileInfo::exists(net.node("bob").downloads));
  QCOMPARE(bob.localPathsForOffer(offer), paths);

  bob.clearCache();
  QVERIFY(bob.localPathsForOffer(offer).isEmpty());
  QVERIFY(!QFileInfo::exists(paths.at(0)));
}

void FileTransferTests::clipboardOffer_staleTokenIsDeclined()
{
  Network net;
  const auto a = net.makeFile("a.txt", "a");
  const auto b = net.makeFile("b.txt", "b");
  const auto stale = FileTransferManager::parseOffer(net.manager("alice").createOffer({a}));
  net.manager("alice").createOffer({b});

  QVERIFY(net.manager("bob").requestOffer(stale));
  net.run();

  QVERIFY(net.node("bob").clipboardPaths.isEmpty());
  QVERIFY(net.node("bob").reports.empty());
  // declined, so asking again really asks again
  QVERIFY(net.manager("bob").requestOffer(stale));
  QVERIFY(net.run() > 0);
}

void FileTransferTests::clipboardTransfer_unrequestedIsRejected()
{
  Network net;
  auto &server = net.manager("server");

  server.handleMessage("alice", {Type::Start, 11, 0, startHeader("clip", 1, 99)});
  net.run();

  QCOMPARE(server.activeCount(), size_t(0));
  QVERIFY(!QFileInfo::exists(net.node("server").cache));
}

QTEST_MAIN(FileTransferTests)
