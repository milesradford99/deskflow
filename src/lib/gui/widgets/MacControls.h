/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QIcon>
#include <QRadioButton>

class QButtonGroup;
class QLabel;
class QScrollArea;
class QVariantAnimation;
class QVBoxLayout;
class QHBoxLayout;

/**
 * Building blocks for the macOS style GUI (see Theme.h): line symbols, a switch,
 * a segmented control, grouped form cards, choice cards and a scrolling page.
 */
namespace deskflow::gui::mac {

//! Line symbols drawn for this app, tinted to the theme.
enum class Symbol
{
  Computers,
  Clipboard,
  Screenshot,
  Sparkle,
  Log,
  Settings,
  Play,
  Stop,
  Restart,
  Pencil,
  Layout,
  Keyboard,
  Cursor,
  Lock,
  Files,
  Export,
  Server,
  Client,
  Remote,
  Shield
};

/**
 * @brief An icon for a symbol.
 * @param color Normal colour; checked (On) state is drawn in @p onColor when that is valid.
 */
QIcon symbol(Symbol name, const QColor &color, const QColor &onColor = QColor());

//! A macOS style on/off switch. Checkable; use toggled() / setChecked() like a check box.
class ToggleSwitch : public QAbstractButton
{
  Q_OBJECT
public:
  explicit ToggleSwitch(QWidget *parent = nullptr);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  void checkStateSet() override;
  void nextCheckState() override;

private:
  void animateTo(bool on);
  qreal m_position = 0; // 0 off, 1 on
  QVariantAnimation *m_animation;
};

//! A row of mutually exclusive buttons in a rounded track, like NSSegmentedControl.
class SegmentedControl : public QFrame
{
  Q_OBJECT
public:
  explicit SegmentedControl(QWidget *parent = nullptr);
  int addSegment(const QString &text, const QIcon &icon = QIcon());
  void setSegmentText(int index, const QString &text);
  void setCurrentIndex(int index);
  int currentIndex() const;
  int count() const;

Q_SIGNALS:
  void currentChanged(int index);

private:
  QHBoxLayout *m_layout;
  QButtonGroup *m_group;
};

/**
 * @brief Settings grouped on a rounded card: one row per setting, the label and an
 * optional explanation on the left, the control on the right, hairlines between rows.
 */
class FormCard : public QFrame
{
  Q_OBJECT
public:
  explicit FormCard(QWidget *parent = nullptr);

  struct Row
  {
    QWidget *widget = nullptr;
    QLabel *title = nullptr;
    QLabel *detail = nullptr;
  };

  /**
   * @brief Add a row.
   * @param control Right aligned control, may be null.
   * @return The row's widgets, so callers can retranslate or hide them.
   */
  Row addRow(const QString &title, const QString &detail = QString(), QWidget *control = nullptr);

  //! Add a full width widget as a row of its own (e.g. a text field under a switch).
  QWidget *addWidget(QWidget *widget);

  //! Hide or show a row and the hairline above it.
  void setRowVisible(QWidget *row, bool visible);

private:
  void updateSeparators();
  QVBoxLayout *m_layout;
  QList<QWidget *> m_rows;
  QList<QFrame *> m_separators;
};

//! A large selectable card with a symbol, a title and a sentence, used for the computer's role.
class ChoiceCard : public QRadioButton
{
  Q_OBJECT
public:
  ChoiceCard(Symbol symbol, QWidget *parent = nullptr);
  void setTitle(const QString &title);
  void setDescription(const QString &description);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  bool hitButton(const QPoint &pos) const override;
  void enterEvent(QEnterEvent *event) override;
  void leaveEvent(QEvent *event) override;

private:
  QRect textRect(int width) const;
  Symbol m_symbol;
  QString m_title;
  QString m_description;
  bool m_hover = false;
};

/**
 * @brief A scrolling page with a large title, an optional subtitle and trailing
 * widget, and a centred column for the content.
 */
class Page : public QWidget
{
  Q_OBJECT
public:
  explicit Page(QWidget *parent = nullptr);

  void setTitle(const QString &title);
  void setSubtitle(const QString &subtitle);
  //! Put a widget (a status pill, a button) at the right of the title.
  void setTrailing(QWidget *widget);
  //! Replace the title label (e.g. with an editable name).
  QHBoxLayout *titleRow() const
  {
    return m_titleRow;
  }
  QLabel *titleLabel() const
  {
    return m_title;
  }
  QLabel *subtitleLabel() const
  {
    return m_subtitle;
  }

  //! Add a small heading above the next card.
  QLabel *addSection(const QString &title);
  void addWidget(QWidget *widget);
  void addLayout(QLayout *layout);
  void addStretch();
  QVBoxLayout *contentLayout() const
  {
    return m_content;
  }

private:
  QScrollArea *m_scroll;
  QLabel *m_title;
  QLabel *m_subtitle;
  QHBoxLayout *m_titleRow;
  QVBoxLayout *m_content;
};

//! Make a label with a text role from Theme.h.
QLabel *label(const QString &text, const char *role, QWidget *parent = nullptr);

} // namespace deskflow::gui::mac
