/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

class QApplication;

//! The application's look: one palette and style sheet for every platform
/*!
Follows Fluent 2: quiet neutral surfaces, content on rounded cards, one accent
colour for the primary action and selection. Light and dark variants follow the
system setting and switch live.

Widgets opt into roles with dynamic properties rather than their own style sheets:
- `card` (QFrame): a raised surface
- `primary` (QPushButton): the main action of a view
- `link` (QPushButton): a text-only action
- `heading` / `secondary` / `error` (QLabel): text roles
- `segment` (QRadioButton): one half of a segmented control
- `status` (QLabel) with `state` = ok | busy | off: a status pill
*/
namespace deskflow::gui::theme {

//! Install the style, palette and style sheet, and keep them in sync with the system scheme.
void apply(QApplication &app);

//! True if the dark variant is in use.
bool isDark();

} // namespace deskflow::gui::theme
