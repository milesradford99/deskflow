/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class RemoteFrameTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void firstFrame_isKeyAndCoversEverything();
  void unchangedFrame_sendsNothingOnceRefined();
  void smallChange_sendsOnlyThatArea();
  void pieces_roundTripAndRebuildPicture();
  void settledArea_isSharpenedLosslessly();
  void readPiece_rejectsTilesOutsideFrame();
  void bigTile_isSplitAndJoinedAgain();
  void h264_roundTripsThroughPiecesAndCanvas();
  void fitSize_keepsAspectAndEvenSides();
};
