/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "InputArbiterTests.h"

#include "client/InputArbiter.h"

#include <QStringList>
#include <QTest>

using deskflow::client::InputArbiter;

namespace {

constexpr InputArbiter::SessionId kA = 1;
constexpr InputArbiter::SessionId kB = 2;
constexpr InputArbiter::SessionId kC = 3;

//! Records what the arbiter does, one short line per call
class Recorder : public InputArbiter::Delegate
{
public:
  void enterComputer(InputArbiter::SessionId session, int32_t x, int32_t y, KeyModifierMask mask) override
  {
    calls << QStringLiteral("enter %1 %2,%3 %4").arg(session).arg(x).arg(y).arg(mask);
  }
  void leaveComputer(InputArbiter::SessionId session) override
  {
    calls << QStringLiteral("leave %1").arg(session);
  }
  void controlLost(InputArbiter::SessionId session) override
  {
    calls << QStringLiteral("lost %1").arg(session);
  }

  QStringList take()
  {
    auto result = calls;
    calls.clear();
    return result;
  }

  QStringList calls;
};

struct Fixture
{
  explicit Fixture(int delay = 2000)
  {
    arbiter.setTakeoverDelay(delay);
    arbiter.addSession(kA);
    arbiter.addSession(kB);
    arbiter.addSession(kC);
  }

  Recorder recorder;
  qint64 now = 1000;
  InputArbiter arbiter{recorder, [this] { return now; }};
};

} // namespace

void InputArbiterTests::enter_noOwner_grants()
{
  Fixture f;
  QVERIFY(f.arbiter.enter(kA, 10, 20, 2));
  QCOMPARE(f.arbiter.owner(), kA);
  QCOMPARE(f.recorder.take(), QStringList{"enter 1 10,20 2"});
}

void InputArbiterTests::enter_ownerBusy_deniesAndTellsServer()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.recorder.take();

  f.now += 500;
  QVERIFY(f.arbiter.input(kA));
  f.now += 500;
  QVERIFY(!f.arbiter.enter(kB, 5, 5, 0));
  QCOMPARE(f.arbiter.owner(), kA);
  QCOMPARE(f.recorder.take(), QStringList{"lost 2"});
}

void InputArbiterTests::enter_ownerIdle_takesOverAndRevokes()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.recorder.take();

  f.now += 2000;
  QVERIFY(f.arbiter.enter(kB, 7, 8, 0));
  QCOMPARE(f.arbiter.owner(), kB);
  QCOMPARE(f.recorder.take(), (QStringList{"leave 1", "lost 1", "enter 2 7,8 0"}));
}

void InputArbiterTests::enter_zeroDelay_latestWins()
{
  Fixture f(0);
  f.arbiter.enter(kA, 0, 0, 0);
  QVERIFY(f.arbiter.input(kA));
  QVERIFY(f.arbiter.enter(kB, 0, 0, 0));
  QCOMPARE(f.arbiter.owner(), kB);
}

void InputArbiterTests::enter_negativeDelay_firstKeepsIt()
{
  Fixture f(-1);
  f.arbiter.enter(kA, 0, 0, 0);
  f.now += 1000 * 3600;
  QVERIFY(!f.arbiter.enter(kB, 0, 0, 0));
  QVERIFY(!f.arbiter.input(kB));
  QCOMPARE(f.arbiter.owner(), kA);
}

void InputArbiterTests::enter_sameServerAgain_staysOwner()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.recorder.take();
  QVERIFY(f.arbiter.enter(kA, 3, 4, 0));
  QCOMPARE(f.recorder.take(), QStringList{"enter 1 3,4 0"});
}

void InputArbiterTests::input_nonOwner_dropped()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  QVERIFY(f.arbiter.input(kA));
  QVERIFY(!f.arbiter.input(kB));
  QVERIFY(!f.arbiter.mouseMove(kB, 1, 1));
}

void InputArbiterTests::input_waitingServerAfterIdle_takesOver()
{
  // a server that doesn't take its cursor back keeps sending; once the owner is idle it wins
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.arbiter.enter(kB, 0, 0, 0);
  QVERIFY(!f.arbiter.input(kB));

  f.now += 2500;
  f.recorder.take();
  QVERIFY(f.arbiter.mouseMove(kB, 40, 50));
  QCOMPARE(f.arbiter.owner(), kB);
  QCOMPARE(f.recorder.take(), (QStringList{"leave 1", "lost 1", "enter 2 40,50 0"}));
}

void InputArbiterTests::input_releaseEvent_neverClaims()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.arbiter.enter(kB, 0, 0, 0);
  f.now += 5000;
  QVERIFY(!f.arbiter.input(kB, false));
  QCOMPARE(f.arbiter.owner(), kA);
}

void InputArbiterTests::input_serverNotEntered_neverClaims()
{
  Fixture f(0);
  f.arbiter.enter(kA, 0, 0, 0);
  QVERIFY(!f.arbiter.input(kB));
  QCOMPARE(f.arbiter.owner(), kA);
}

void InputArbiterTests::leave_owner_handsToLatestWaiting()
{
  Fixture f(-1);
  f.arbiter.enter(kA, 0, 0, 0);
  f.arbiter.enter(kB, 1, 1, 0);
  f.arbiter.enter(kC, 2, 2, 4);
  f.recorder.take();

  f.arbiter.leave(kA);
  QCOMPARE(f.arbiter.owner(), kC);
  QCOMPARE(f.recorder.take(), (QStringList{"leave 1", "enter 3 2,2 4"}));

  // nobody waiting: the computer is free
  f.arbiter.leave(kB);
  f.arbiter.leave(kC);
  QCOMPARE(f.arbiter.owner(), InputArbiter::kNoSession);
}

void InputArbiterTests::removeSession_owner_releasesAndHandsOver()
{
  Fixture f(-1);
  f.arbiter.enter(kA, 0, 0, 0);
  f.arbiter.enter(kB, 9, 9, 0);
  f.recorder.take();

  f.arbiter.removeSession(kA);
  QVERIFY(!f.arbiter.hasSession(kA));
  QCOMPARE(f.arbiter.owner(), kB);
  QCOMPARE(f.recorder.take(), (QStringList{"leave 1", "enter 2 9,9 0"}));
}

void InputArbiterTests::removeSession_nonOwner_ownerUntouched()
{
  Fixture f;
  f.arbiter.enter(kA, 0, 0, 0);
  f.recorder.take();
  f.arbiter.removeSession(kB);
  QCOMPARE(f.arbiter.owner(), kA);
  QVERIFY(f.recorder.take().isEmpty());
}

void InputArbiterTests::mouseMove_remembersPositionForHandover()
{
  Fixture f(-1);
  f.arbiter.enter(kA, 0, 0, 0);
  f.arbiter.enter(kB, 0, 0, 0);
  f.arbiter.mouseMove(kB, 30, 31); // dropped, but remembered
  f.recorder.take();
  f.arbiter.leave(kA);
  QCOMPARE(f.recorder.take(), (QStringList{"leave 1", "enter 2 30,31 0"}));
}

QTEST_MAIN(InputArbiterTests)
