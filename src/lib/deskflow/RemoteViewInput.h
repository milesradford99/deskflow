/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"
#include "deskflow/MouseTypes.h"

#include <QString>

//! Turns keys and buttons the GUI's remote viewer got from Qt into what clients understand
namespace deskflow::remoteViewInput {

struct Key
{
  KeyID id = kKeyNone;
  KeyModifierMask mask = 0;
};

//! \c qtKey is a Qt::Key, \c modifiers Qt::KeyboardModifiers; on macOS Qt's Control is Command
Key translateKey(int qtKey, const QString &text, int modifiers);

//! Qt::KeyboardModifiers to a deskflow modifier mask
KeyModifierMask translateModifiers(int modifiers);

//! Qt::MouseButton to a deskflow button, kButtonNone if there's no equivalent
ButtonID translateButton(int qtButton);

} // namespace deskflow::remoteViewInput
