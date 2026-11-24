/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QKeySequence>
#include <QString>

#include <optional>

class QWidget;

namespace deskflow::gui::clipboard {

/**
 * @brief Ask for a single chord, system wide shortcut.
 * @param prompt What the shortcut does, e.g. "Press the keys you want to use to take a screenshot."
 * @param command The command line flag that does the same thing, shown where global shortcuts are unsupported.
 * @return The chosen sequence (possibly empty, meaning none), or nothing when cancelled.
 */
std::optional<QKeySequence> askForGlobalShortcut(
    QWidget *parent, const QString &title, const QString &prompt, const QString &command, const QKeySequence &current,
    const QKeySequence &fallback
);

} // namespace deskflow::gui::clipboard
