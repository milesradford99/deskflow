/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "KeyDisplayTests.h"

#include "deskflow/KeyDisplay.h"

using deskflow::formatKeyForDisplay;

void KeyDisplayTests::plainCharacter_isText()
{
  const auto d = formatKeyForDisplay('a', 0);
  QCOMPARE(d.label, QStringLiteral("a"));
  QVERIFY(d.isText);
}

void KeyDisplayTests::shiftedCharacter_isText()
{
  const auto d = formatKeyForDisplay('A', KeyModifierShift);
  QCOMPARE(d.label, QStringLiteral("A"));
  QVERIFY(d.isText);

  const auto altGr = formatKeyForDisplay('@', KeyModifierAltGr);
  QCOMPARE(altGr.label, QStringLiteral("@"));
  QVERIFY(altGr.isText);
}

void KeyDisplayTests::space_isText()
{
  const auto d = formatKeyForDisplay(' ', 0);
  QCOMPARE(d.label, QStringLiteral("␣"));
  QVERIFY(d.isText);

  QCOMPARE(formatKeyForDisplay(' ', KeyModifierControl).label, QStringLiteral("Ctrl+Space"));
}

void KeyDisplayTests::ctrlLetter_isUpperCaseCombo()
{
  const auto d = formatKeyForDisplay('c', KeyModifierControl);
  QCOMPARE(d.label, QStringLiteral("Ctrl+C"));
  QVERIFY(!d.isText);

  QCOMPARE(formatKeyForDisplay('v', KeyModifierSuper).label, QStringLiteral("Super+V"));
  QCOMPARE(formatKeyForDisplay(kKeyTab, KeyModifierAlt).label, QStringLiteral("Alt+Tab"));
}

void KeyDisplayTests::ctrlShiftLetter_keepsShift()
{
  QCOMPARE(formatKeyForDisplay('T', KeyModifierControl | KeyModifierShift).label, QStringLiteral("Ctrl+Shift+T"));
}

void KeyDisplayTests::ctrlShiftSymbol_dropsShift()
{
  QCOMPARE(formatKeyForDisplay('?', KeyModifierControl | KeyModifierShift).label, QStringLiteral("Ctrl+?"));
}

void KeyDisplayTests::loneModifier_noDuplicate()
{
  const auto d = formatKeyForDisplay(kKeyControl_L, KeyModifierControl);
  QCOMPARE(d.label, QStringLiteral("Ctrl"));
  QVERIFY(!d.isText);

  QCOMPARE(formatKeyForDisplay(kKeyShift_R, KeyModifierShift | KeyModifierControl).label, QStringLiteral("Ctrl+Shift"));
}

void KeyDisplayTests::specialKeys_haveFriendlyNames()
{
  QCOMPARE(formatKeyForDisplay(kKeyReturn, 0).label, QStringLiteral("↵"));
  QCOMPARE(formatKeyForDisplay(kKeyBackSpace, 0).label, QStringLiteral("⌫"));
  QCOMPARE(formatKeyForDisplay(kKeyEscape, 0).label, QStringLiteral("Esc"));
  QCOMPARE(formatKeyForDisplay(kKeyF5, 0).label, QStringLiteral("F5"));
  QCOMPARE(formatKeyForDisplay(kKeyLeft, KeyModifierShift).label, QStringLiteral("Shift+←"));
  QCOMPARE(formatKeyForDisplay(kKeyKP_3, 0).label, QStringLiteral("Num 3"));
}

void KeyDisplayTests::lockModifiers_areIgnored()
{
  const auto d = formatKeyForDisplay('a', KeyModifierNumLock | KeyModifierCapsLock);
  QCOMPARE(d.label, QStringLiteral("a"));
  QVERIFY(d.isText);
}

void KeyDisplayTests::deadKey_isHidden()
{
  QVERIFY(formatKeyForDisplay(kKeyDeadAcute, 0).label.isEmpty());
  QVERIFY(formatKeyForDisplay(kKeyNone, 0).label.isEmpty());
}

QTEST_MAIN(KeyDisplayTests)
