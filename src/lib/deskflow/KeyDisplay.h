/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <QString>

namespace deskflow {

/**
 * @brief Human readable form of a key press, used by the activity overlay.
 *
 * `isText` is true when the press produces ordinary text (a printable character,
 * optionally with Shift), so the overlay can join consecutive presses into a word.
 * Anything else (shortcuts, special keys, lone modifiers) is a separate combo.
 */
struct KeyDisplay
{
  QString label;
  bool isText = false;
};

/**
 * @brief Formats a key press for display, e.g. `Ctrl+Shift+T`, `Enter`, `a`.
 *
 * Returns an empty label for keys that should not be shown on their own
 * (e.g. dead keys or unknown private-use ids).
 */
KeyDisplay formatKeyForDisplay(KeyID id, KeyModifierMask mask);

} // namespace deskflow
