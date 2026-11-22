/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class KeyDisplayTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void plainCharacter_isText();
  void shiftedCharacter_isText();
  void space_isText();
  void ctrlLetter_isUpperCaseCombo();
  void ctrlShiftLetter_keepsShift();
  void ctrlShiftSymbol_dropsShift();
  void loneModifier_noDuplicate();
  void specialKeys_haveFriendlyNames();
  void lockModifiers_areIgnored();
  void deadKey_isHidden();
  void modifierKeys_areDetected();
};
