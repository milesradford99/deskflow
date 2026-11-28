/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/RemoteViewInput.h"

#include <Qt>

#include <unordered_map>

namespace deskflow::remoteViewInput {

namespace {

// Qt swaps Command and Control on macOS: ControlModifier/Key_Control is Command there
#ifdef Q_OS_MACOS
constexpr int kCommandModifier = Qt::ControlModifier;
constexpr int kControlModifier = Qt::MetaModifier;
constexpr int kCommandKey = Qt::Key_Control;
constexpr int kControlKey = Qt::Key_Meta;
#else
constexpr int kCommandModifier = Qt::MetaModifier; // the Windows / Super key
constexpr int kControlModifier = Qt::ControlModifier;
constexpr int kCommandKey = Qt::Key_Meta;
constexpr int kControlKey = Qt::Key_Control;
#endif

const std::unordered_map<int, KeyID> &specialKeys()
{
  static const std::unordered_map<int, KeyID> keys = {
      {Qt::Key_Escape, kKeyEscape},
      {Qt::Key_Tab, kKeyTab},
      {Qt::Key_Backtab, kKeyTab},
      {Qt::Key_Backspace, kKeyBackSpace},
      {Qt::Key_Return, kKeyReturn},
      {Qt::Key_Enter, kKeyKP_Enter},
      {Qt::Key_Insert, kKeyInsert},
      {Qt::Key_Delete, kKeyDelete},
      {Qt::Key_Pause, kKeyPause},
      {Qt::Key_Print, kKeyPrint},
      {Qt::Key_SysReq, kKeySysReq},
      {Qt::Key_Clear, kKeyClear},
      {Qt::Key_Home, kKeyHome},
      {Qt::Key_End, kKeyEnd},
      {Qt::Key_Left, kKeyLeft},
      {Qt::Key_Up, kKeyUp},
      {Qt::Key_Right, kKeyRight},
      {Qt::Key_Down, kKeyDown},
      {Qt::Key_PageUp, kKeyPageUp},
      {Qt::Key_PageDown, kKeyPageDown},
      {Qt::Key_Shift, kKeyShift_L},
      {kControlKey, kKeyControl_L},
      {kCommandKey, kKeySuper_L},
      {Qt::Key_Alt, kKeyAlt_L},
      {Qt::Key_AltGr, kKeyAltGr},
      {Qt::Key_CapsLock, kKeyCapsLock},
      {Qt::Key_NumLock, kKeyNumLock},
      {Qt::Key_ScrollLock, kKeyScrollLock},
      {Qt::Key_Menu, kKeyMenu},
      {Qt::Key_Help, kKeyHelp},
      {Qt::Key_Super_L, kKeySuper_L},
      {Qt::Key_Super_R, kKeySuper_R},
      {Qt::Key_VolumeDown, kKeyAudioDown},
      {Qt::Key_VolumeUp, kKeyAudioUp},
      {Qt::Key_VolumeMute, kKeyAudioMute},
      {Qt::Key_MediaPlay, kKeyAudioPlay},
      {Qt::Key_MediaTogglePlayPause, kKeyAudioPlay},
      {Qt::Key_MediaStop, kKeyAudioStop},
      {Qt::Key_MediaNext, kKeyAudioNext},
      {Qt::Key_MediaPrevious, kKeyAudioPrev},
      {Qt::Key_MonBrightnessDown, kKeyBrightnessDown},
      {Qt::Key_MonBrightnessUp, kKeyBrightnessUp},
      {Qt::Key_Back, kKeyWWWBack},
      {Qt::Key_Forward, kKeyWWWForward},
      {Qt::Key_Refresh, kKeyWWWRefresh},
      {Qt::Key_Henkan, kKeyHenkan},
      {Qt::Key_Kana_Lock, kKeyKana},
      {Qt::Key_Zenkaku_Hankaku, kKeyZenkaku},
      {Qt::Key_Hangul, kKeyHangul},
      {Qt::Key_Hangul_Hanja, kKeyHanja},
  };
  return keys;
}

KeyID keypadKey(int qtKey)
{
  if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
    return static_cast<KeyID>(kKeyKP_0 + (qtKey - Qt::Key_0));
  switch (qtKey) {
  case Qt::Key_Asterisk:
    return kKeyKP_Multiply;
  case Qt::Key_Plus:
    return kKeyKP_Add;
  case Qt::Key_Minus:
    return kKeyKP_Subtract;
  case Qt::Key_Period:
    return kKeyKP_Decimal;
  case Qt::Key_Slash:
    return kKeyKP_Divide;
  case Qt::Key_Equal:
    return kKeyKP_Equal;
  case Qt::Key_Enter:
    return kKeyKP_Enter;
  default:
    return kKeyNone;
  }
}

} // namespace

KeyModifierMask translateModifiers(int modifiers)
{
  KeyModifierMask mask = 0;
  if (modifiers & Qt::ShiftModifier)
    mask |= KeyModifierShift;
  if (modifiers & kControlModifier)
    mask |= KeyModifierControl;
  if (modifiers & Qt::AltModifier)
    mask |= KeyModifierAlt;
  if (modifiers & kCommandModifier)
    mask |= KeyModifierSuper;
  return mask;
}

Key translateKey(int qtKey, const QString &text, int modifiers)
{
  Key key;
  key.mask = translateModifiers(modifiers);

  if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F35) {
    key.id = static_cast<KeyID>(kKeyF1 + (qtKey - Qt::Key_F1));
    return key;
  }
  if (modifiers & Qt::KeypadModifier) {
    if (const auto id = keypadKey(qtKey); id != kKeyNone) {
      key.id = id;
      return key;
    }
  }
  if (const auto it = specialKeys().find(qtKey); it != specialKeys().end()) {
    key.id = it->second;
    return key;
  }

  // typed text is what the user meant, unless a shortcut modifier makes it a combination
  const bool shortcut = modifiers & (kControlModifier | kCommandModifier | Qt::AltModifier);
  if (!shortcut && text.size() == 1 && text.at(0).unicode() >= 0x20 && text.at(0).unicode() != 0x7f) {
    key.id = text.at(0).unicode();
    return key;
  }
  if (!shortcut && text.size() == 2 && text.at(0).isHighSurrogate()) {
    key.id = QChar::surrogateToUcs4(text.at(0), text.at(1));
    return key;
  }

  if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
    key.id = static_cast<KeyID>((modifiers & Qt::ShiftModifier) ? qtKey : qtKey - Qt::Key_A + 'a');
    return key;
  }
  if (qtKey >= Qt::Key_Space && qtKey <= Qt::Key_AsciiTilde) {
    key.id = static_cast<KeyID>(qtKey);
    return key;
  }
  if (qtKey > 0xff && qtKey < 0x01000000) {
    key.id = static_cast<KeyID>(qtKey); // other Latin-1 / Unicode keys Qt reports by code point
    return key;
  }
  return key;
}

ButtonID translateButton(int qtButton)
{
  switch (qtButton) {
  case Qt::LeftButton:
    return kButtonLeft;
  case Qt::RightButton:
    return kButtonRight;
  case Qt::MiddleButton:
    return kButtonMiddle;
  case Qt::BackButton:
    return kButtonExtra0;
  case Qt::ForwardButton:
    return kButtonExtra1;
  default:
    return kButtonNone;
  }
}

} // namespace deskflow::remoteViewInput
