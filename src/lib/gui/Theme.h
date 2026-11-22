/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

class QApplication;
class QColor;

//! The application's look: one palette and style sheet for every platform
/*!
Follows macOS conventions, shared with the owner's DayOfFlow app: warm neutral
surfaces, a sidebar for navigation, settings grouped on rounded cards with one
row per setting (label on the left, control on the right), capsule buttons and
one quiet iris accent. Light and dark variants follow the system and switch live.

Widgets opt into roles with dynamic properties rather than their own style sheets:
- `card` (QFrame): a grouped surface; `rowSeparator` (QFrame) a hairline between rows
- `banner` (QFrame): a tinted notice, e.g. "restart to apply"
- `primary` / `large` / `destructive` / `iconOnly` / `link` (QPushButton)
- `nav` (QPushButton): a sidebar row; `segment` (QPushButton) inside a `segmentTrack` (QFrame)
- `title` / `heading` / `eyebrow` / `secondary` / `tertiary` / `rowTitle` / `rowDetail` /
  `error` / `keycap` (QLabel): text roles
- `status` (QLabel) with `state` = ok | busy | off: a status pill
*/
namespace deskflow::gui::theme {

//! Install the style, palette and style sheet, and keep them in sync with the system scheme.
void apply(QApplication &app);

//! True if the dark variant is in use.
bool isDark();

//! Colours for custom painted controls, in the variant in use.
QColor accent();
QColor textSecondary();
QColor border();

} // namespace deskflow::gui::theme
