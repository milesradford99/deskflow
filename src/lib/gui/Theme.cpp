/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Theme.h"

#include "StyleUtils.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

namespace deskflow::gui::theme {

namespace {

struct Colors
{
  const char *window;
  const char *card;
  const char *cardAlt; // inputs and rows inside a card
  const char *border;
  const char *borderStrong;
  const char *text;
  const char *textSecondary;
  const char *textDisabled;
  const char *accent;
  const char *accentHover;
  const char *accentPressed;
  const char *onAccent;
  const char *hover; // neutral hover fill
  const char *pressed;
  const char *error;
  const char *ok;
  const char *busy;
};

constexpr Colors kLight{"#f5f5f5", "#ffffff", "#fafafa", "#e0e0e0", "#c7c7c7", "#242424",
                        "#616161", "#a6a6a6", "#0f6cbd", "#115ea3", "#0c3b5e", "#ffffff",
                        "#ebebeb", "#e0e0e0", "#c50f1f", "#107c10", "#bc4b09"};

constexpr Colors kDark{"#1f1f1f", "#292929", "#333333", "#3d3d3d", "#575757", "#ffffff",
                       "#adadad", "#6b6b6b", "#479ef5", "#62abf5", "#2886de", "#0a0a0a",
                       "#383838", "#424242", "#f1707b", "#54b054", "#f7a35c"};

bool s_dark = false;

bool systemIsDark()
{
  // lets screenshots and tests pin a scheme whatever the desktop says
  if (const auto forced = qEnvironmentVariable("DESKFLOW_COLOR_SCHEME"); !forced.isEmpty())
    return forced.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0;

  switch (QGuiApplication::styleHints()->colorScheme()) {
  case Qt::ColorScheme::Dark:
    return true;
  case Qt::ColorScheme::Light:
    return false;
  default:
    return isDarkMode();
  }
}

QPalette paletteFor(const Colors &c)
{
  QPalette palette;
  palette.setColor(QPalette::Window, c.window);
  palette.setColor(QPalette::WindowText, c.text);
  palette.setColor(QPalette::Base, c.card);
  palette.setColor(QPalette::AlternateBase, c.cardAlt);
  palette.setColor(QPalette::Text, c.text);
  palette.setColor(QPalette::Button, c.card);
  palette.setColor(QPalette::ButtonText, c.text);
  palette.setColor(QPalette::ToolTipBase, c.card);
  palette.setColor(QPalette::ToolTipText, c.text);
  palette.setColor(QPalette::PlaceholderText, c.textSecondary);
  palette.setColor(QPalette::Highlight, c.accent);
  palette.setColor(QPalette::HighlightedText, c.onAccent);
  palette.setColor(QPalette::Link, c.accent);
  palette.setColor(QPalette::Mid, c.border);
  palette.setColor(QPalette::Dark, c.borderStrong);
  palette.setColor(QPalette::Light, c.card);
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    palette.setColor(QPalette::Disabled, role, c.textDisabled);
  return palette;
}

QString styleSheetFor(const Colors &c)
{
  // %N placeholders are filled from Colors below; keep the two lists in step
  static const auto kSheet = QStringLiteral(R"(
QWidget { font-size: 13px; }
QMainWindow, QDialog { background: %1; }
QToolTip { background: %2; color: %6; border: 1px solid %4; border-radius: 4px; padding: 4px 8px; }

QLabel[heading="true"] { font-size: 15px; font-weight: 600; }
QLabel[title="true"] { font-size: 20px; font-weight: 600; }
QLabel[secondary="true"] { color: %7; }
QLabel[error="true"] { color: %15; }
QLabel[status="true"] { border-radius: 11px; padding: 3px 10px; font-weight: 600; background: %13; color: %7; }
QLabel[status="true"][state="ok"] { color: %16; }
QLabel[status="true"][state="busy"] { color: %17; }

QFrame[card="true"], QGroupBox {
  background: %2; border: 1px solid %4; border-radius: 8px;
}
QFrame[card="true"][dropActive="true"] { border: 1px dashed %9; }
QGroupBox { margin-top: 0; padding: 12px; }
QGroupBox::title { subcontrol-origin: padding; left: 12px; top: 8px; font-weight: 600; }
QFrame[transferRow="true"] { background: %3; border: 1px solid %4; border-radius: 6px; }

QPushButton {
  background: %2; color: %6; border: 1px solid %5; border-radius: 6px; padding: 6px 14px; min-height: 18px;
}
QPushButton:hover { background: %13; }
QPushButton:pressed { background: %14; }
QPushButton:disabled { color: %8; border-color: %4; }
QPushButton:default, QPushButton[primary="true"] { background: %9; color: %12; border-color: %9; font-weight: 600; }
QPushButton:default:hover, QPushButton[primary="true"]:hover { background: %10; border-color: %10; }
QPushButton:default:pressed, QPushButton[primary="true"]:pressed { background: %11; border-color: %11; }
QPushButton[primary="true"]:disabled { background: %14; color: %8; border-color: %14; }
QPushButton:flat { border: none; background: transparent; }
QPushButton:flat:hover { background: %13; }
QPushButton[link="true"] { color: %9; border: none; background: transparent; padding: 2px 4px; }
QPushButton[link="true"]:hover { color: %10; text-decoration: underline; }

QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 4px 8px; color: %6; }
QToolButton:hover { background: %13; }
QToolButton:pressed { background: %14; }
QToolButton[dropTarget="true"] { background: %3; border: 1px solid %5; padding: 8px 14px; font-weight: 600; }
QToolButton[dropTarget="true"]:hover { border-color: %9; }
QToolButton[dropTarget="true"][dropActive="true"] { background: %9; color: %12; border-color: %9; }
QToolButton[dropTarget="true"]::menu-button { border: none; width: 18px; }

QRadioButton[segment="true"] {
  background: %2; border: 1px solid %5; padding: 10px 14px; spacing: 0;
}
QRadioButton[segment="true"]::indicator { width: 0; height: 0; }
QRadioButton[segment="true"]:hover { background: %13; }
QRadioButton[segment="true"]:checked { background: %9; color: %12; border-color: %9; font-weight: 600; }
QRadioButton[segment="first"], QRadioButton[segment="true"][segmentPosition="first"] {
  border-top-left-radius: 6px; border-bottom-left-radius: 6px;
}
QRadioButton[segment="true"][segmentPosition="last"] {
  border-top-right-radius: 6px; border-bottom-right-radius: 6px; border-left: none;
}

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit, QPlainTextEdit, QTextEdit {
  background: %2; color: %6; border: 1px solid %5; border-radius: 6px; padding: 5px 8px;
  selection-background-color: %9; selection-color: %12;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
  border: 1px solid %9;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: %8; background: %3; border-color: %4; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView {
  background: %2; border: 1px solid %4; border-radius: 6px; outline: none; selection-background-color: %13;
  selection-color: %6;
}

QCheckBox, QRadioButton { spacing: 8px; }

QMenuBar { background: %1; }
QMenuBar::item { padding: 5px 10px; border-radius: 4px; background: transparent; }
QMenuBar::item:selected { background: %13; }
QMenu { background: %2; border: 1px solid %4; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: %13; color: %6; }
QMenu::item:disabled { color: %8; }
QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }

QTabWidget::pane { border: 1px solid %4; border-radius: 8px; background: %2; top: -1px; }
QTabBar::tab { padding: 7px 14px; border: none; border-bottom: 2px solid transparent; color: %7; background: transparent; }
QTabBar::tab:hover { color: %6; }
QTabBar::tab:selected { color: %6; border-bottom: 2px solid %9; font-weight: 600; }

QProgressBar { background: %4; border: none; border-radius: 2px; }
QProgressBar::chunk { background: %9; border-radius: 2px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %5; border-radius: 3px; min-height: 28px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %5; border-radius: 3px; min-width: 28px; }
QScrollBar::handle:hover { background: %7; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; height: 0; width: 0; }

QHeaderView::section { background: %3; border: none; border-bottom: 1px solid %4; padding: 6px 8px; font-weight: 600; }
QTableView, QListView, QTreeView { background: %2; border: 1px solid %4; border-radius: 6px; outline: none; }
QStatusBar { background: %1; color: %7; }
QStatusBar::item { border: none; }
QDockWidget::title { padding: 6px 8px; font-weight: 600; }
)");

  return kSheet
      .arg(c.window, c.card, c.cardAlt, c.border, c.borderStrong, c.text, c.textSecondary, c.textDisabled, c.accent)
      .arg(c.accentHover, c.accentPressed, c.onAccent, c.hover, c.pressed, c.error, c.ok, c.busy);
}

void refresh(QApplication &app)
{
  s_dark = systemIsDark();
  const auto &colors = s_dark ? kDark : kLight;
  app.setPalette(paletteFor(colors));
  app.setStyleSheet(styleSheetFor(colors));
  updateIconTheme();
}

} // namespace

void apply(QApplication &app)
{
  // Fusion is the one style that draws the same everywhere and honours the whole style sheet
  QApplication::setStyle(QStringLiteral("fusion"));
  refresh(app);
  QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] { refresh(app); });
}

bool isDark()
{
  return s_dark;
}

} // namespace deskflow::gui::theme
