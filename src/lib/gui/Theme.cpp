/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Theme.h"

#include "StyleUtils.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleHints>

namespace deskflow::gui::theme {

namespace {

struct Colors
{
  const char *window;  // content background: warm paper white / deep graphite
  const char *sidebar; // the navigation column
  const char *card;    // grouped settings and panels
  const char *inset;   // inputs and tracks inside a card
  const char *border;  // hairlines
  const char *borderStrong;
  const char *text;
  const char *textSecondary;
  const char *textTertiary;
  const char *accent;
  const char *accentHover;
  const char *accentPressed;
  const char *onAccent;
  const char *accentSoft; // selection tint
  const char *hover;      // neutral hover fill
  const char *pressed;
  const char *error;
  const char *ok;
  const char *busy;
  const char *navSelected; // selected sidebar row
};

// Shared with DayOfFlow: a quiet iris accent on warm neutrals, light and dark.
constexpr Colors kLight{"#f6f5f2", "#efeeea", "#ffffff", "#f3f2ef", "#e6e5e1", "#d4d3ce", "#1c1c1e",
                        "#6b6b70", "#9c9ca2", "#5a55d6", "#4f4ac8", "#4440b3", "#ffffff", "#ebeafa",
                        "#e9e8e4", "#e0dfdb", "#c2415b", "#2e8b62", "#c9772b", "#5a55d6"};

constexpr Colors kDark{"#161618", "#1c1c1f", "#1f1f22", "#28282c", "#2c2c30", "#3c3c41", "#f2f2f3",
                       "#a1a1a8", "#6e6e75", "#8b87f5", "#9d99f7", "#7773e3", "#ffffff", "#2b2a45",
                       "#2a2a2e", "#333337", "#f07a90", "#5cc796", "#e9a15f", "#4f4bb8"};

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
  palette.setColor(QPalette::AlternateBase, c.inset);
  palette.setColor(QPalette::Text, c.text);
  palette.setColor(QPalette::Button, c.card);
  palette.setColor(QPalette::ButtonText, c.text);
  palette.setColor(QPalette::ToolTipBase, c.card);
  palette.setColor(QPalette::ToolTipText, c.text);
  palette.setColor(QPalette::PlaceholderText, c.textTertiary);
  palette.setColor(QPalette::Highlight, c.accent);
  palette.setColor(QPalette::HighlightedText, c.onAccent);
  palette.setColor(QPalette::Link, c.accent);
  palette.setColor(QPalette::LinkVisited, c.error);
  palette.setColor(QPalette::Mid, c.border);
  palette.setColor(QPalette::Dark, c.borderStrong);
  palette.setColor(QPalette::Light, c.card);
  palette.setColor(QPalette::Midlight, c.inset);
  palette.setColor(QPalette::BrightText, c.textSecondary);
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    palette.setColor(QPalette::Disabled, role, c.textTertiary);
  return palette;
}

QString styleSheetFor(const Colors &c)
{
  // %N placeholders are filled from Colors below; keep the two lists in step:
  // %1 window %2 sidebar %3 card %4 inset %5 border %6 borderStrong %7 text %8 textSecondary %9 textTertiary
  // %10 accent %11 accentHover %12 accentPressed %13 onAccent %14 accentSoft %15 hover %16 pressed
  // %17 error %18 ok %19 busy %20 navSelected
  //
  // Shapes: capsules for everything pressed (buttons, pills), 8 px for fields and rows,
  // 12 px for tiles, 16 px for cards.
  static const auto kSheet = QStringLiteral(R"(
QWidget { font-size: 13px; }
QMainWindow, QDialog, QStackedWidget#pages { background: %1; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget[page="true"] { background: %1; }
QToolTip { background: %3; color: %7; border: 1px solid %5; border-radius: 6px; padding: 4px 8px; }

QLabel[title="true"] { font-size: 24px; font-weight: 700; }
QLabel[heading="true"] { font-size: 15px; font-weight: 600; }
QLabel[eyebrow="true"] { font-size: 12px; font-weight: 600; color: %8; padding-left: 4px; }
QLabel#statusDot, QLabel[dot="true"] { border-radius: 4px; background: %9; }
QLabel[dot="true"][state="ok"] { background: %18; }
QLabel[dot="true"][state="busy"] { background: %19; }
QLabel[rowTitle="true"] { font-size: 13px; }
QLabel[secondary="true"], QLabel[rowDetail="true"] { color: %8; }
QLabel[rowDetail="true"] { font-size: 12px; }
QLabel[tertiary="true"] { color: %9; font-size: 12px; }
QLabel[error="true"] { color: %17; }
QLabel[status="true"] { border-radius: 11px; padding: 3px 11px; font-weight: 600; background: %4; color: %8; }
QLabel[status="true"][state="ok"] { color: %18; }
QLabel[status="true"][state="busy"] { color: %19; }
QLabel[status="true"][state="warn"] { color: %17; }
QLabel[badge="true"] {
  background: %17; color: #ffffff; border-radius: 8px; padding: 0 5px; font-size: 11px; font-weight: 700;
  min-width: 6px; min-height: 16px; max-height: 16px;
}
QLabel[keycap="true"] {
  background: %4; color: %8; border: 1px solid %5; border-radius: 5px; padding: 1px 6px; font-size: 12px;
}

QFrame#sidebar { background: %2; border: none; border-right: 1px solid %5; }
QLabel[brand="true"] { font-size: 15px; font-weight: 700; }
QPushButton[nav="true"] {
  text-align: left; border: none; border-radius: 8px; padding: 6px 10px; background: transparent; color: %7;
  font-size: 13px; min-height: 18px;
}
QPushButton[nav="true"]:hover { background: %15; }
QPushButton[nav="true"]:checked { background: %20; color: #ffffff; font-weight: 600; }
QFrame[sideCard="true"] { background: %3; border: 1px solid %5; border-radius: 12px; }

QFrame[card="true"], QGroupBox {
  background: %3; border: 1px solid %5; border-radius: 16px;
}
QFrame[card="true"][dropActive="true"] { border: 1px dashed %10; }
QFrame[rowSeparator="true"] { background: %5; border: none; max-height: 1px; min-height: 1px; }
QFrame[banner="true"] { background: %14; border: 1px solid %14; border-radius: 12px; }
QGroupBox { margin-top: 0; padding: 12px; }
QGroupBox::title { subcontrol-origin: padding; left: 12px; top: 8px; font-weight: 600; }
QFrame[transferRow="true"] { background: %4; border: none; border-radius: 12px; }

QPushButton {
  background: %3; color: %7; border: 1px solid %6; border-radius: 14px; padding: 5px 14px; min-height: 16px;
}
QPushButton:hover { background: %15; }
QPushButton:pressed { background: %16; }
QPushButton:disabled { color: %9; border-color: %5; }
QPushButton:default, QPushButton[primary="true"] { background: %10; color: %13; border-color: %10; }
QPushButton[primary="true"] { font-weight: 600; }
QPushButton:default:hover, QPushButton[primary="true"]:hover { background: %11; border-color: %11; }
QPushButton:default:pressed, QPushButton[primary="true"]:pressed { background: %12; border-color: %12; }
QPushButton[primary="true"]:disabled { background: %16; color: %9; border-color: %16; }
QPushButton[large="true"] { border-radius: 18px; padding: 8px 22px; min-height: 18px; font-size: 14px; }
QPushButton[destructive="true"] { color: %17; }
QPushButton:flat, QPushButton[iconOnly="true"] { border: none; background: transparent; border-radius: 8px; padding: 4px; }
QPushButton:flat:hover, QPushButton[iconOnly="true"]:hover { background: %15; }
QPushButton[link="true"] { color: %10; border: none; background: transparent; padding: 2px 4px; }
QPushButton[link="true"]:hover { color: %11; }

QFrame[segmentTrack="true"] { background: %4; border: 1px solid %5; border-radius: 9px; }
QPushButton[segment="true"] {
  background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 4px 14px; color: %8;
  min-height: 16px;
}
QPushButton[segment="true"]:hover { color: %7; }
QPushButton[segment="true"]:checked { background: %3; color: %7; border-color: %5; }

QToolButton { border: 1px solid transparent; border-radius: 8px; padding: 4px 8px; color: %7; }
QToolButton:hover { background: %15; }
QToolButton:pressed { background: %16; }
QToolButton[dropTarget="true"] { background: %4; border: 1px solid %5; border-radius: 14px; padding: 6px 14px; font-weight: 600; }
QToolButton[dropTarget="true"]:hover { border-color: %10; }
QToolButton[dropTarget="true"][dropActive="true"] { background: %10; color: %13; border-color: %10; }
QToolButton[dropTarget="true"]::menu-button { border: none; width: 18px; }
QToolButton[editorTool="true"] { padding: 4px; border-radius: 7px; }
QToolButton[editorTool="true"]:checked { background: %3; border-color: %5; }
QFrame[editorBar="true"] { background: %1; border: none; border-bottom: 1px solid %5; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit, QPlainTextEdit, QTextEdit {
  background: %4; color: %7; border: 1px solid %5; border-radius: 8px; padding: 4px 8px;
  selection-background-color: %10; selection-color: %13;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
  border: 1px solid %10; background: %3;
}
QLineEdit[titleEdit="true"] { font-size: 22px; font-weight: 700; padding: 0 6px; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: %9; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
  background: %3; border: 1px solid %5; border-radius: 8px; outline: none; selection-background-color: %14;
  selection-color: %7; padding: 4px;
}

QCheckBox, QRadioButton { spacing: 8px; }

QMenuBar { background: %1; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: %15; }
QMenu { background: %3; border: 1px solid %5; border-radius: 10px; padding: 5px; }
QMenu::item { padding: 5px 24px 5px 12px; border-radius: 6px; }
QMenu::item:selected { background: %10; color: %13; }
QMenu::item:disabled { color: %9; }
QMenu::separator { height: 1px; background: %5; margin: 4px 8px; }

QTabWidget::pane { border: 1px solid %5; border-radius: 12px; background: %3; top: -1px; }
QTabBar::tab { padding: 7px 14px; border: none; border-bottom: 2px solid transparent; color: %8; background: transparent; }
QTabBar::tab:hover { color: %7; }
QTabBar::tab:selected { color: %7; border-bottom: 2px solid %10; font-weight: 600; }

QProgressBar { background: %5; border: none; border-radius: 2px; }
QProgressBar::chunk { background: %10; border-radius: 2px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %6; border-radius: 3px; min-height: 28px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %6; border-radius: 3px; min-width: 28px; }
QScrollBar::handle:hover { background: %9; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; height: 0; width: 0; }

QHeaderView::section { background: %4; border: none; border-bottom: 1px solid %5; padding: 6px 8px; font-weight: 600; }
QTableView, QListView, QTreeView { background: %3; border: 1px solid %5; border-radius: 12px; outline: none; }
QPlainTextEdit[log="true"] { background: %3; border: 1px solid %5; border-radius: 12px; padding: 8px; }
QStatusBar { background: %1; color: %8; border-top: 1px solid %5; }
QStatusBar QLabel { color: %8; font-size: 12px; }
QStatusBar::item { border: none; }
QDockWidget::title { padding: 6px 8px; font-weight: 600; }
)");

  return kSheet
      .arg(c.window, c.sidebar, c.card, c.inset, c.border, c.borderStrong, c.text, c.textSecondary, c.textTertiary)
      .arg(c.accent, c.accentHover, c.accentPressed, c.onAccent, c.accentSoft, c.hover, c.pressed, c.error, c.ok)
      .arg(c.busy, c.navSelected);
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

QColor accent()
{
  return QColor(s_dark ? kDark.accent : kLight.accent);
}

QColor textSecondary()
{
  return QColor(s_dark ? kDark.textSecondary : kLight.textSecondary);
}

QColor border()
{
  return QColor(s_dark ? kDark.borderStrong : kLight.borderStrong);
}

} // namespace deskflow::gui::theme
