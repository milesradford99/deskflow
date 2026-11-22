/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryTests.h"

#include "gui/clipboard/ClipboardHistoryFilterModel.h"
#include "gui/clipboard/ClipboardHistoryModel.h"
#include "gui/clipboard/ClipboardMonitor.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QUrl>

using namespace deskflow::gui::clipboard;

namespace {
HistoryItem textItem(const QString &text)
{
  QMimeData mime;
  mime.setText(text);
  return *ClipboardMonitor::itemFromMimeData(&mime, nullptr);
}
} // namespace

void ClipboardHistoryTests::initTestCase()
{
  QDir(m_storage).removeRecursively();
}

void ClipboardHistoryTests::addAndDeduplicate()
{
  ClipboardHistoryModel model({});
  const auto first = model.add(textItem("alpha"));
  model.add(textItem("beta"));
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.itemAt(0)->text, "beta");

  // Copying the same text again moves it to the top instead of duplicating it.
  const auto again = model.add(textItem("alpha"));
  QCOMPARE(again, first);
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.itemAt(0)->text, "alpha");
  QCOMPARE(model.itemAt(0)->copyCount, 2);
}

void ClipboardHistoryTests::pinnedSurvivesTrimAndClear()
{
  ClipboardHistoryModel model({});
  model.setMaxItems(3);
  const auto pinned = model.add(textItem("keep me"));
  model.setPinned(pinned, true);
  for (int i = 0; i < 10; ++i)
    model.add(textItem(QStringLiteral("item %1").arg(i)));

  // Three unpinned items plus the pinned one.
  QCOMPARE(model.rowCount(), 4);
  QVERIFY(model.item(pinned));
  QCOMPARE(model.itemAt(0)->text, "item 9");

  model.clear(true);
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.itemAt(0)->id, pinned);

  model.clear(false);
  QCOMPARE(model.rowCount(), 0);
}

void ClipboardHistoryTests::persistence()
{
  QString pinnedId;
  {
    ClipboardHistoryModel model(m_storage);
    model.add(textItem("first"));
    pinnedId = model.add(textItem("second"));
    model.setPinned(pinnedId, true);
    model.setSource(pinnedId, "laptop");

    QImage image(4, 3, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QMimeData mime;
    mime.setImageData(image);
    QImage captured;
    auto item = ClipboardMonitor::itemFromMimeData(&mime, &captured);
    QVERIFY(item.has_value());
    QCOMPARE(item->kind, ItemKind::Image);
    model.add(*item, captured);
    model.saveNow();
  }

  ClipboardHistoryModel reloaded(m_storage);
  QCOMPARE(reloaded.rowCount(), 3);
  QCOMPARE(reloaded.itemAt(0)->kind, ItemKind::Image);
  QCOMPARE(reloaded.itemAt(0)->imageSize, QSize(4, 3));
  QCOMPARE(reloaded.image(reloaded.itemAt(0)->id).size(), QSize(4, 3));
  const auto *pinned = reloaded.item(pinnedId);
  QVERIFY(pinned);
  QVERIFY(pinned->pinned);
  QCOMPARE(pinned->source, "laptop");

  // Removing an image item deletes its file.
  const auto imageFile = QStringLiteral("%1/images/%2").arg(m_storage, reloaded.itemAt(0)->imageFile);
  QVERIFY(QFile::exists(imageFile));
  reloaded.remove(reloaded.itemAt(0)->id);
  QVERIFY(!QFile::exists(imageFile));
}

void ClipboardHistoryTests::searchScoring()
{
  QVERIFY(ClipboardHistoryFilterModel::matchScore("hello world", "") == 0);
  QVERIFY(ClipboardHistoryFilterModel::matchScore("hello world", "xyz") < 0);
  QVERIFY(ClipboardHistoryFilterModel::matchScore("hello world", "WORLD") > 0);
  // Fuzzy subsequence.
  QVERIFY(ClipboardHistoryFilterModel::matchScore("deskflow config", "dsk cfg") > 0);
  // All words must match.
  QVERIFY(ClipboardHistoryFilterModel::matchScore("deskflow config", "deskflow zebra") < 0);
  // Word start beats mid word, substring beats fuzzy.
  const int wordStart = ClipboardHistoryFilterModel::matchScore("the flow", "flow");
  const int midWord = ClipboardHistoryFilterModel::matchScore("deskflow", "flow");
  const int fuzzy = ClipboardHistoryFilterModel::matchScore("f-l-o-w", "flow");
  QVERIFY(wordStart > midWord);
  QVERIFY(midWord > fuzzy);
}

void ClipboardHistoryTests::filterAndSort()
{
  ClipboardHistoryModel model({});
  const auto often = model.add(textItem("used often"));
  model.add(textItem("https://deskflow.org"));
  model.add(textItem("used once"));
  model.touch(often);
  model.touch(often);
  const auto pinned = model.add(textItem("pinned note"));
  model.setPinned(pinned, true);
  model.add(textItem("newest"));

  ClipboardHistoryFilterModel filter;
  filter.setSourceModel(&model);

  // Pinned first, then most recent.
  QCOMPARE(filter.index(0, 0).data(ClipboardHistoryModel::IdRole).toString(), pinned);
  QCOMPARE(filter.index(1, 0).data(ClipboardHistoryModel::TextRole).toString(), "newest");

  filter.setSortMode(ClipboardHistoryFilterModel::SortMode::MostUsed);
  QCOMPARE(filter.index(1, 0).data(ClipboardHistoryModel::IdRole).toString(), often);

  filter.setCategory(ClipboardHistoryFilterModel::Category::Links);
  QCOMPARE(filter.rowCount(), 1);
  QCOMPARE(filter.index(0, 0).data(ClipboardHistoryModel::TextRole).toString(), "https://deskflow.org");

  filter.setCategory(ClipboardHistoryFilterModel::Category::Pinned);
  QCOMPARE(filter.rowCount(), 1);

  filter.setCategory(ClipboardHistoryFilterModel::Category::All);
  filter.setSearchText("used");
  QCOMPARE(filter.rowCount(), 2);
}

void ClipboardHistoryTests::detectKinds()
{
  QCOMPARE(ClipboardMonitor::detectTextKind("https://example.com/a?b=c"), ItemKind::Link);
  QCOMPARE(ClipboardMonitor::detectTextKind("  www.example.com  "), ItemKind::Link);
  QCOMPARE(ClipboardMonitor::detectTextKind("#ff8800"), ItemKind::Color);
  QCOMPARE(ClipboardMonitor::detectTextKind("#abc"), ItemKind::Color);
  QCOMPARE(ClipboardMonitor::detectTextKind("#notacolor"), ItemKind::Text);
  QCOMPARE(ClipboardMonitor::detectTextKind("see https://example.com"), ItemKind::Text);

  QMimeData files;
  files.setUrls({QUrl::fromLocalFile("/tmp/a.txt"), QUrl::fromLocalFile("/tmp/b.txt")});
  auto item = ClipboardMonitor::itemFromMimeData(&files, nullptr);
  QVERIFY(item.has_value());
  QCOMPARE(item->kind, ItemKind::Files);
  QCOMPARE(item->urls.size(), 2);

  QMimeData blank;
  blank.setText("   \n ");
  QVERIFY(!ClipboardMonitor::itemFromMimeData(&blank, nullptr).has_value());
}

void ClipboardHistoryTests::sensitiveContentIsSkipped()
{
  QMimeData kde;
  kde.setText("hunter2");
  kde.setData("x-kde-passwordManagerHint", "secret");
  QVERIFY(ClipboardMonitor::isSensitive(&kde));

  QMimeData windows;
  windows.setText("hunter2");
  windows.setData("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\"", QByteArray(4, '\0'));
  QVERIFY(ClipboardMonitor::isSensitive(&windows));

  QMimeData normal;
  normal.setText("hello");
  QVERIFY(!ClipboardMonitor::isSensitive(&normal));
}

void ClipboardHistoryTests::sourceFromLogLines()
{
  const auto remote = ClipboardMonitor::remoteSourceFromLogLine(
      "[2026-01-01T00:00:00] INFO: computer \"laptop\" updated clipboard 0", "desktop"
  );
  QCOMPARE(remote, std::optional<QString>("laptop"));

  const auto local =
      ClipboardMonitor::remoteSourceFromLogLine("INFO: computer \"Desktop\" updated clipboard 0", "desktop");
  QVERIFY(local.has_value());
  QVERIFY(local->isEmpty());

  // Primary selection updates are not clipboard copies.
  QVERIFY(!ClipboardMonitor::remoteSourceFromLogLine("computer \"laptop\" updated clipboard 1", "desktop"));

  QCOMPARE(
      ClipboardMonitor::remoteSourceFromLogLine("INFO: clipboard was updated", "desktop"),
      std::optional<QString>(ClipboardMonitor::kFromServer)
  );
  QVERIFY(!ClipboardMonitor::remoteSourceFromLogLine("INFO: connected to server", "desktop"));
}

void ClipboardHistoryTests::monitorRecordsAndAttributes()
{
  ClipboardHistoryModel model({});
  ClipboardMonitor monitor(&model);
  monitor.setLocalComputerName("desktop");

  // Log line first, then the clipboard change.
  monitor.handleCoreLogLine("INFO: computer \"laptop\" updated clipboard 0");
  QGuiApplication::clipboard()->setText("from the laptop");
  monitor.readClipboard();
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.itemAt(0)->source, "laptop");

  // Clipboard change first, then the log line.
  QGuiApplication::clipboard()->setText("from the tablet");
  monitor.readClipboard();
  monitor.handleCoreLogLine("INFO: computer \"tablet\" updated clipboard 0");
  QCOMPARE(model.itemAt(0)->source, "tablet");

  // Paused: nothing recorded.
  monitor.setPaused(true);
  QGuiApplication::clipboard()->setText("while paused");
  monitor.readClipboard();
  QCOMPARE(model.rowCount(), 2);
  monitor.setPaused(false);

  // Ignore next copy only skips one.
  monitor.ignoreNextCopy();
  QGuiApplication::clipboard()->setText("secret");
  monitor.readClipboard();
  QCOMPARE(model.rowCount(), 2);
  QVERIFY(!monitor.isIgnoringNextCopy());
  QGuiApplication::clipboard()->setText("local copy");
  monitor.readClipboard();
  QCOMPARE(model.rowCount(), 3);
  QVERIFY(model.itemAt(0)->source.isEmpty());
}

void ClipboardHistoryTests::copyBackDoesNotDuplicate()
{
  ClipboardHistoryModel model({});
  ClipboardMonitor monitor(&model);

  QGuiApplication::clipboard()->setText("one");
  monitor.readClipboard();
  QGuiApplication::clipboard()->setText("two");
  monitor.readClipboard();
  const auto oneId = model.itemAt(1)->id;

  QVERIFY(monitor.copyToClipboard(oneId));
  QCOMPARE(QGuiApplication::clipboard()->text(), "one");
  monitor.readClipboard();

  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.itemAt(0)->id, oneId);
  QCOMPARE(model.itemAt(0)->copyCount, 2);
}

QTEST_MAIN(ClipboardHistoryTests)
