/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class InputArbiterTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void enter_noOwner_grants();
  void enter_ownerBusy_deniesAndTellsServer();
  void enter_ownerIdle_takesOverAndRevokes();
  void enter_zeroDelay_latestWins();
  void enter_negativeDelay_firstKeepsIt();
  void enter_sameServerAgain_staysOwner();
  void input_nonOwner_dropped();
  void input_waitingServerAfterIdle_takesOver();
  void input_releaseEvent_neverClaims();
  void input_serverNotEntered_neverClaims();
  void leave_owner_handsToLatestWaiting();
  void removeSession_owner_releasesAndHandsOver();
  void removeSession_nonOwner_ownerUntouched();
  void mouseMove_remembersPositionForHandover();

private:
  Log m_log;
};
