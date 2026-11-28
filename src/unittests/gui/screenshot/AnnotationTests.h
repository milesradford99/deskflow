/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class AnnotationTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void renderCropsAndFlattens();
  void emptyMarks();
  void hitTesting();
  void mosaicPixelates();
  void canvasUndoRedo();
};
