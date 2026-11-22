/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/KeyDisplay.h"

#include <QStringList>

namespace deskflow {

namespace {

// Modifier key ids map to the modifier bit they set, so a lone `Ctrl` press
// renders as `Ctrl` instead of `Ctrl+Ctrl`.
KeyModifierMask modifierForKey(KeyID id)
{
  switch (id) {
  case kKeyShift_L:
  case kKeyShift_R:
    return KeyModifierShift;
  case kKeyControl_L:
  case kKeyControl_R:
    return KeyModifierControl;
  case kKeyAlt_L:
  case kKeyAlt_R:
    return KeyModifierAlt;
  case kKeyMeta_L:
  case kKeyMeta_R:
    return KeyModifierMeta;
  case kKeySuper_L:
  case kKeySuper_R:
    return KeyModifierSuper;
  case kKeyAltGr:
    return KeyModifierAltGr;
  default:
    return 0;
  }
}

QString specialKeyName(KeyID id)
{
  if (id >= kKeyF1 && id <= kKeyF35)
    return QStringLiteral("F%1").arg(id - kKeyF1 + 1);
  if (id >= kKeyKP_0 && id <= kKeyKP_9)
    return QStringLiteral("Num %1").arg(id - kKeyKP_0);

  switch (id) {
  case kKeyBackSpace:
    return QStringLiteral("⌫");
  case kKeyTab:
  case kKeyKP_Tab:
    return QStringLiteral("Tab");
  case kKeyReturn:
  case kKeyKP_Enter:
  case kKeyLinefeed:
    return QStringLiteral("↵");
  case kKeyEscape:
    return QStringLiteral("Esc");
  case kKeyDelete:
  case kKeyKP_Delete:
    return QStringLiteral("Del");
  case kKeyHome:
  case kKeyKP_Home:
    return QStringLiteral("Home");
  case kKeyEnd:
  case kKeyKP_End:
    return QStringLiteral("End");
  case kKeyLeft:
  case kKeyKP_Left:
    return QStringLiteral("←");
  case kKeyRight:
  case kKeyKP_Right:
    return QStringLiteral("→");
  case kKeyUp:
  case kKeyKP_Up:
    return QStringLiteral("↑");
  case kKeyDown:
  case kKeyKP_Down:
    return QStringLiteral("↓");
  case kKeyPageUp:
  case kKeyKP_PageUp:
    return QStringLiteral("PgUp");
  case kKeyPageDown:
  case kKeyKP_PageDown:
    return QStringLiteral("PgDn");
  case kKeyInsert:
  case kKeyKP_Insert:
    return QStringLiteral("Ins");
  case kKeyPrint:
    return QStringLiteral("PrtSc");
  case kKeyPause:
  case kKeyBreak:
    return QStringLiteral("Pause");
  case kKeyMenu:
    return QStringLiteral("Menu");
  case kKeyCapsLock:
    return QStringLiteral("CapsLock");
  case kKeyNumLock:
    return QStringLiteral("NumLock");
  case kKeyScrollLock:
    return QStringLiteral("ScrollLock");
  case kKeyKP_Space:
    return QStringLiteral("Space");
  case kKeyKP_Add:
    return QStringLiteral("Num +");
  case kKeyKP_Subtract:
    return QStringLiteral("Num -");
  case kKeyKP_Multiply:
    return QStringLiteral("Num *");
  case kKeyKP_Divide:
    return QStringLiteral("Num /");
  case kKeyKP_Decimal:
    return QStringLiteral("Num .");
  case kKeyKP_Equal:
    return QStringLiteral("Num =");
  case kKeyShift_L:
  case kKeyShift_R:
    return QStringLiteral("Shift");
  case kKeyControl_L:
  case kKeyControl_R:
    return QStringLiteral("Ctrl");
  case kKeyAlt_L:
  case kKeyAlt_R:
    return QStringLiteral("Alt");
  case kKeyMeta_L:
  case kKeyMeta_R:
    return QStringLiteral("Meta");
  case kKeySuper_L:
  case kKeySuper_R:
    return QStringLiteral("Super");
  case kKeyAltGr:
    return QStringLiteral("AltGr");
  default:
    return {};
  }
}

bool isPrintable(KeyID id)
{
  // Ids outside the private-use control range (and not combining dead keys) are UTF-32 characters.
  if (id < 0x20 || id == 0x7f)
    return false;
  if (id >= 0xE000 && id <= 0xEFFF)
    return false;
  if (id >= kKeyDeadGrave && id <= 0x036F)
    return false;
  return id <= 0x10FFFF;
}

} // namespace

KeyDisplay formatKeyForDisplay(KeyID id, KeyModifierMask mask)
{
  // Locks are state, not part of a chord.
  mask &=
      (KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierMeta | KeyModifierSuper | KeyModifierAltGr);

  const auto ownModifier = modifierForKey(id);
  mask &= ~ownModifier;

  // AltGr (and Shift) only pick a different character, so typing `@` with AltGr is still text.
  const auto chordMask = mask & ~(KeyModifierShift | KeyModifierAltGr);
  const bool printable = isPrintable(id);

  if (printable && chordMask == 0) {
    if (id == 0x20)
      return {QStringLiteral("␣"), true};
    return {QString::fromUcs4(reinterpret_cast<const char32_t *>(&id), 1), true};
  }

  QString key;
  if (printable) {
    // Shortcuts read better upper-case: Ctrl+C rather than Ctrl+c.
    key =
        id == 0x20 ? QStringLiteral("Space") : QString::fromUcs4(reinterpret_cast<const char32_t *>(&id), 1).toUpper();
  } else {
    key = specialKeyName(id);
  }

  if (key.isEmpty())
    return {};

  QStringList parts;
  if (mask & KeyModifierControl)
    parts << QStringLiteral("Ctrl");
  if (mask & KeyModifierAlt)
    parts << QStringLiteral("Alt");
  if (mask & KeyModifierAltGr)
    parts << QStringLiteral("AltGr");
  if (mask & KeyModifierMeta)
    parts << QStringLiteral("Meta");
  if (mask & KeyModifierSuper)
    parts << QStringLiteral("Super");
  // Shift is implied by the character itself for printable keys (e.g. Ctrl+? rather than Ctrl+Shift+?),
  // except for letters where it changes the shortcut (Ctrl+Shift+T).
  const bool shiftMatters = !printable || QChar::isLetter(static_cast<char32_t>(id));
  if ((mask & KeyModifierShift) && shiftMatters)
    parts << QStringLiteral("Shift");
  parts << key;

  return {parts.join(QLatin1Char('+')), false};
}

} // namespace deskflow
