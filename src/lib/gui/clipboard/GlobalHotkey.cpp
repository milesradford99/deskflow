/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "GlobalHotkey.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QHash>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#elif defined(WINAPI_XWINDOWS) && QT_CONFIG(xcb) && __has_include(<xcb/xcb.h>)
#define DESKFLOW_X11_HOTKEY 1
// X11 headers define macros (None, Bool, KeyPress...) that clash with Qt, so they come last.
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#endif

namespace deskflow::gui::clipboard {

namespace {

#if defined(Q_OS_WIN)
constexpr int kHotkeyId = 0xDF01;

quint32 nativeKeycode(Qt::Key key)
{
  if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
    return quint32(key);
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
    return VK_F1 + (key - Qt::Key_F1);

  static const QHash<int, quint32> table = {
      {Qt::Key_Space, VK_SPACE},    {Qt::Key_Escape, VK_ESCAPE}, {Qt::Key_Tab, VK_TAB},
      {Qt::Key_Backspace, VK_BACK}, {Qt::Key_Return, VK_RETURN}, {Qt::Key_Enter, VK_RETURN},
      {Qt::Key_Insert, VK_INSERT},  {Qt::Key_Delete, VK_DELETE}, {Qt::Key_Pause, VK_PAUSE},
      {Qt::Key_Print, VK_SNAPSHOT}, {Qt::Key_Home, VK_HOME},     {Qt::Key_End, VK_END},
      {Qt::Key_Left, VK_LEFT},      {Qt::Key_Up, VK_UP},         {Qt::Key_Right, VK_RIGHT},
      {Qt::Key_Down, VK_DOWN},      {Qt::Key_PageUp, VK_PRIOR},  {Qt::Key_PageDown, VK_NEXT},
  };
  if (auto it = table.constFind(key); it != table.constEnd())
    return *it;

  if (key < 0x100) {
    const SHORT scan = VkKeyScanW(wchar_t(QChar(char16_t(key)).toLower().unicode()));
    if (scan != -1)
      return LOBYTE(scan);
  }
  return 0;
}

quint32 nativeModifiers(Qt::KeyboardModifiers mods)
{
  quint32 native = MOD_NOREPEAT;
  if (mods & Qt::ShiftModifier)
    native |= MOD_SHIFT;
  if (mods & Qt::ControlModifier)
    native |= MOD_CONTROL;
  if (mods & Qt::AltModifier)
    native |= MOD_ALT;
  if (mods & Qt::MetaModifier)
    native |= MOD_WIN;
  return native;
}

#elif defined(Q_OS_MACOS)
constexpr OSType kSignature = (OSType('D') << 24) | (OSType('F') << 16) | (OSType('C') << 8) | OSType('H');

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef event, void *userData)
{
  EventHotKeyID hotkeyId;
  if (GetEventParameter(
          event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotkeyId), nullptr, &hotkeyId
      ) == noErr &&
      hotkeyId.signature == kSignature) {
    static_cast<GlobalHotkey *>(userData)->trigger();
  }
  return noErr;
}

quint32 nativeKeycode(Qt::Key key)
{
  static const QHash<int, quint32> table = {
      {Qt::Key_A, kVK_ANSI_A},
      {Qt::Key_B, kVK_ANSI_B},
      {Qt::Key_C, kVK_ANSI_C},
      {Qt::Key_D, kVK_ANSI_D},
      {Qt::Key_E, kVK_ANSI_E},
      {Qt::Key_F, kVK_ANSI_F},
      {Qt::Key_G, kVK_ANSI_G},
      {Qt::Key_H, kVK_ANSI_H},
      {Qt::Key_I, kVK_ANSI_I},
      {Qt::Key_J, kVK_ANSI_J},
      {Qt::Key_K, kVK_ANSI_K},
      {Qt::Key_L, kVK_ANSI_L},
      {Qt::Key_M, kVK_ANSI_M},
      {Qt::Key_N, kVK_ANSI_N},
      {Qt::Key_O, kVK_ANSI_O},
      {Qt::Key_P, kVK_ANSI_P},
      {Qt::Key_Q, kVK_ANSI_Q},
      {Qt::Key_R, kVK_ANSI_R},
      {Qt::Key_S, kVK_ANSI_S},
      {Qt::Key_T, kVK_ANSI_T},
      {Qt::Key_U, kVK_ANSI_U},
      {Qt::Key_V, kVK_ANSI_V},
      {Qt::Key_W, kVK_ANSI_W},
      {Qt::Key_X, kVK_ANSI_X},
      {Qt::Key_Y, kVK_ANSI_Y},
      {Qt::Key_Z, kVK_ANSI_Z},
      {Qt::Key_0, kVK_ANSI_0},
      {Qt::Key_1, kVK_ANSI_1},
      {Qt::Key_2, kVK_ANSI_2},
      {Qt::Key_3, kVK_ANSI_3},
      {Qt::Key_4, kVK_ANSI_4},
      {Qt::Key_5, kVK_ANSI_5},
      {Qt::Key_6, kVK_ANSI_6},
      {Qt::Key_7, kVK_ANSI_7},
      {Qt::Key_8, kVK_ANSI_8},
      {Qt::Key_9, kVK_ANSI_9},
      {Qt::Key_F1, kVK_F1},
      {Qt::Key_F2, kVK_F2},
      {Qt::Key_F3, kVK_F3},
      {Qt::Key_F4, kVK_F4},
      {Qt::Key_F5, kVK_F5},
      {Qt::Key_F6, kVK_F6},
      {Qt::Key_F7, kVK_F7},
      {Qt::Key_F8, kVK_F8},
      {Qt::Key_F9, kVK_F9},
      {Qt::Key_F10, kVK_F10},
      {Qt::Key_F11, kVK_F11},
      {Qt::Key_F12, kVK_F12},
      {Qt::Key_F13, kVK_F13},
      {Qt::Key_F14, kVK_F14},
      {Qt::Key_F15, kVK_F15},
      {Qt::Key_F16, kVK_F16},
      {Qt::Key_F17, kVK_F17},
      {Qt::Key_F18, kVK_F18},
      {Qt::Key_F19, kVK_F19},
      {Qt::Key_F20, kVK_F20},
      {Qt::Key_Space, kVK_Space},
      {Qt::Key_Escape, kVK_Escape},
      {Qt::Key_Tab, kVK_Tab},
      {Qt::Key_Backspace, kVK_Delete},
      {Qt::Key_Delete, kVK_ForwardDelete},
      {Qt::Key_Return, kVK_Return},
      {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
      {Qt::Key_Home, kVK_Home},
      {Qt::Key_End, kVK_End},
      {Qt::Key_PageUp, kVK_PageUp},
      {Qt::Key_PageDown, kVK_PageDown},
      {Qt::Key_Left, kVK_LeftArrow},
      {Qt::Key_Right, kVK_RightArrow},
      {Qt::Key_Up, kVK_UpArrow},
      {Qt::Key_Down, kVK_DownArrow},
      {Qt::Key_QuoteLeft, kVK_ANSI_Grave},
      {Qt::Key_Minus, kVK_ANSI_Minus},
      {Qt::Key_Equal, kVK_ANSI_Equal},
      {Qt::Key_BracketLeft, kVK_ANSI_LeftBracket},
      {Qt::Key_BracketRight, kVK_ANSI_RightBracket},
      {Qt::Key_Backslash, kVK_ANSI_Backslash},
      {Qt::Key_Semicolon, kVK_ANSI_Semicolon},
      {Qt::Key_Apostrophe, kVK_ANSI_Quote},
      {Qt::Key_Comma, kVK_ANSI_Comma},
      {Qt::Key_Period, kVK_ANSI_Period},
      {Qt::Key_Slash, kVK_ANSI_Slash},
  };
  return table.value(key, quint32(-1));
}

quint32 nativeModifiers(Qt::KeyboardModifiers mods)
{
  // Qt swaps Control and Command on macOS: ControlModifier is the Command key.
  quint32 native = 0;
  if (mods & Qt::ShiftModifier)
    native |= shiftKey;
  if (mods & Qt::ControlModifier)
    native |= cmdKey;
  if (mods & Qt::AltModifier)
    native |= optionKey;
  if (mods & Qt::MetaModifier)
    native |= controlKey;
  return native;
}

#elif defined(DESKFLOW_X11_HOTKEY)
constexpr unsigned int kRelevantMods = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;
// Grab with every combination of the lock modifiers so Caps/Num Lock do not break the shortcut.
constexpr unsigned int kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};

bool s_grabFailed = false;
int grabErrorHandler(Display *, XErrorEvent *event)
{
  if (event->error_code == BadAccess)
    s_grabFailed = true;
  return 0;
}

Display *x11Display()
{
  auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
  return x11 ? x11->display() : nullptr;
}

KeySym keysymFor(Qt::Key key)
{
  if (key >= 0x20 && key <= 0xff)
    return KeySym(QChar(char16_t(key)).toLower().unicode());
  if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
    return XK_F1 + (key - Qt::Key_F1);

  static const QHash<int, KeySym> table = {
      {Qt::Key_Escape, XK_Escape}, {Qt::Key_Tab, XK_Tab},        {Qt::Key_Backspace, XK_BackSpace},
      {Qt::Key_Return, XK_Return}, {Qt::Key_Enter, XK_KP_Enter}, {Qt::Key_Insert, XK_Insert},
      {Qt::Key_Delete, XK_Delete}, {Qt::Key_Pause, XK_Pause},    {Qt::Key_Print, XK_Print},
      {Qt::Key_Home, XK_Home},     {Qt::Key_End, XK_End},        {Qt::Key_Left, XK_Left},
      {Qt::Key_Up, XK_Up},         {Qt::Key_Right, XK_Right},    {Qt::Key_Down, XK_Down},
      {Qt::Key_PageUp, XK_Prior},  {Qt::Key_PageDown, XK_Next},
  };
  return table.value(key, NoSymbol);
}

unsigned int nativeModifiers(Qt::KeyboardModifiers mods)
{
  unsigned int native = 0;
  if (mods & Qt::ShiftModifier)
    native |= ShiftMask;
  if (mods & Qt::ControlModifier)
    native |= ControlMask;
  if (mods & Qt::AltModifier)
    native |= Mod1Mask;
  if (mods & Qt::MetaModifier)
    native |= Mod4Mask;
  return native;
}
#endif

} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent)
{
  // Nothing to do until a shortcut is set.
}

GlobalHotkey::~GlobalHotkey()
{
  unregisterNative();
  if (m_filterInstalled && QCoreApplication::instance())
    QCoreApplication::instance()->removeNativeEventFilter(this);
#if defined(Q_OS_MACOS)
  if (m_platformHandler)
    RemoveEventHandler(static_cast<EventHandlerRef>(m_platformHandler));
#endif
}

bool GlobalHotkey::isSupported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return true;
#elif defined(DESKFLOW_X11_HOTKEY)
  return x11Display() != nullptr;
#else
  return false;
#endif
}

bool GlobalHotkey::setShortcut(const QKeySequence &sequence)
{
  unregisterNative();
  m_sequence = sequence;

  if (sequence.isEmpty())
    return true;

  if (!isSupported()) {
    qDebug() << "global shortcuts are not supported on this platform";
    return false;
  }

  if (!m_filterInstalled) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    m_filterInstalled = true;
  }

  const auto combination = sequence[0];
  m_registered = registerNative(combination.key(), combination.keyboardModifiers());
  if (!m_registered)
    qWarning().noquote() << "unable to register global shortcut" << sequence.toString(QKeySequence::NativeText)
                         << "- it may be in use by another application";
  return m_registered;
}

void GlobalHotkey::trigger()
{
  Q_EMIT activated();
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
  Q_UNUSED(result)
  if (!m_registered)
    return false;

#if defined(Q_OS_WIN)
  if (eventType == "windows_generic_MSG") {
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == WPARAM(kHotkeyId)) {
      trigger();
      return true;
    }
  }
#elif defined(DESKFLOW_X11_HOTKEY)
  if (eventType == "xcb_generic_event_t") {
    const auto *event = static_cast<xcb_generic_event_t *>(message);
    if ((event->response_type & ~0x80) == XCB_KEY_PRESS) {
      const auto *keyEvent = static_cast<const xcb_key_press_event_t *>(message);
      if (keyEvent->detail == m_nativeKey && (keyEvent->state & kRelevantMods) == m_nativeMods) {
        trigger();
        return true;
      }
    }
  }
#else
  Q_UNUSED(eventType)
  Q_UNUSED(message)
#endif
  return false;
}

bool GlobalHotkey::registerNative(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
#if defined(Q_OS_WIN)
  m_nativeKey = nativeKeycode(key);
  m_nativeMods = nativeModifiers(modifiers);
  if (m_nativeKey == 0)
    return false;
  return RegisterHotKey(nullptr, kHotkeyId, m_nativeMods, m_nativeKey);

#elif defined(Q_OS_MACOS)
  m_nativeKey = nativeKeycode(key);
  m_nativeMods = nativeModifiers(modifiers);
  if (m_nativeKey == quint32(-1))
    return false;

  if (!m_platformHandler) {
    EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(&hotkeyHandler, 1, &spec, this, &handler) != noErr)
      return false;
    m_platformHandler = handler;
  }

  EventHotKeyID hotkeyId{kSignature, 1};
  EventHotKeyRef ref = nullptr;
  if (RegisterEventHotKey(m_nativeKey, m_nativeMods, hotkeyId, GetApplicationEventTarget(), 0, &ref) != noErr)
    return false;
  m_platformRef = ref;
  return true;

#elif defined(DESKFLOW_X11_HOTKEY)
  auto *display = x11Display();
  if (!display)
    return false;

  const auto keysym = keysymFor(key);
  if (keysym == NoSymbol)
    return false;
  const auto keycode = XKeysymToKeycode(display, keysym);
  if (keycode == 0)
    return false;

  m_nativeKey = keycode;
  m_nativeMods = nativeModifiers(modifiers);

  XSync(display, False);
  s_grabFailed = false;
  auto *previous = XSetErrorHandler(grabErrorHandler);
  const auto root = DefaultRootWindow(display);
  for (const auto lock : kLockMasks)
    XGrabKey(display, keycode, m_nativeMods | lock, root, True, GrabModeAsync, GrabModeAsync);
  XSync(display, False);
  XSetErrorHandler(previous);

  if (s_grabFailed) {
    for (const auto lock : kLockMasks)
      XUngrabKey(display, keycode, m_nativeMods | lock, root);
    XSync(display, False);
    return false;
  }
  return true;

#else
  Q_UNUSED(key)
  Q_UNUSED(modifiers)
  return false;
#endif
}

void GlobalHotkey::unregisterNative()
{
  if (!m_registered)
    return;
  m_registered = false;

#if defined(Q_OS_WIN)
  UnregisterHotKey(nullptr, kHotkeyId);
#elif defined(Q_OS_MACOS)
  if (m_platformRef) {
    UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_platformRef));
    m_platformRef = nullptr;
  }
#elif defined(DESKFLOW_X11_HOTKEY)
  if (auto *display = x11Display()) {
    const auto root = DefaultRootWindow(display);
    for (const auto lock : kLockMasks)
      XUngrabKey(display, m_nativeKey, m_nativeMods | lock, root);
    XSync(display, False);
  }
#endif
}

} // namespace deskflow::gui::clipboard
