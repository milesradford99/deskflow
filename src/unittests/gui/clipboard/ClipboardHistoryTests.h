/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class ClipboardHistoryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void addAndDeduplicate();
  void pinnedSurvivesTrimAndClear();
  void persistence();
  void searchScoring();
  void filterAndSort();
  void detectKinds();
  void sensitiveContentIsSkipped();
  void sourceFromLogLines();
  void monitorRecordsAndAttributes();
  void copyBackDoesNotDuplicate();

private:
  inline static const QString m_storage = QStringLiteral("tmp/clipboard-history-test");
};
