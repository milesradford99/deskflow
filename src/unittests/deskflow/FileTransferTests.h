/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class FileTransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void clientToServer_storesFilesInDownloadDir();
  void serverToClient_keepsFolderStructure();
  void clientToClient_isRelayedByServer();
  void largeFile_staysInsideFlowControlWindow();
  void existingName_getsNumberedCopy();
  void emptySelection_sendsNothing();
  void unknownTarget_failsSender();
  void cancelBySender_removesPartialDownload();
  void cancelByReceiver_stopsSender();
  void pathTraversal_isRejected();
  void truncatedStream_isRejected();
  void receiverDisconnect_failsRelayedSender();
  void clipboardOffer_isStableForSamePaths();
  void clipboardOffer_isFetchedThroughRelay();
  void clipboardOffer_staleTokenIsDeclined();
  void clipboardTransfer_unrequestedIsRejected();

private:
  Log m_log;
};
