/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ShortcutDialog.h"

#include "GlobalHotkey.h"

#include "common/Constants.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace deskflow::gui::clipboard {

// The strings keep the translation context they were first written in, for the clipboard history shortcut.

std::optional<QKeySequence> askForGlobalShortcut(
    QWidget *parent, const QString &title, const QString &prompt, const QString &command, const QKeySequence &current,
    const QKeySequence &fallback
)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(title);
  auto *layout = new QVBoxLayout(&dialog);

  auto *label = new QLabel(&dialog);
  label->setWordWrap(true);
  if (GlobalHotkey::isSupported()) {
    label->setText(prompt);
  } else {
    label->setText(
        QCoreApplication::translate(
            "deskflow::gui::clipboard::ClipboardHistory",
            "Your desktop does not let apps register global shortcuts (for example on Wayland). "
            "Add a custom shortcut in your system keyboard settings that runs:\n\n    %1 --%2"
        )
            .arg(kAppId, command)
    );
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  layout->addWidget(label);

  auto *edit = new QKeySequenceEdit(current, &dialog);
  edit->setEnabled(GlobalHotkey::isSupported());
  // Only single chord shortcuts make sense globally.
  QObject::connect(edit, &QKeySequenceEdit::keySequenceChanged, edit, [edit](const QKeySequence &sequence) {
    if (sequence.count() > 1)
      edit->setKeySequence(QKeySequence(sequence[0]));
  });
  layout->addWidget(edit);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto *reset = buttons->addButton(
      QCoreApplication::translate("deskflow::gui::clipboard::ClipboardHistory", "Use default"),
      QDialogButtonBox::ResetRole
  );
  QObject::connect(reset, &QPushButton::clicked, edit, [edit, fallback] { edit->setKeySequence(fallback); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted)
    return std::nullopt;
  return edit->keySequence();
}

} // namespace deskflow::gui::clipboard
