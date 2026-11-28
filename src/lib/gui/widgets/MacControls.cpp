/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacControls.h"

#include "gui/Theme.h"

#include <QButtonGroup>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace deskflow::gui::mac {

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

namespace {

//! Draw a symbol on a 24 x 24 grid with the painter's pen.
void drawSymbol(QPainter &p, Symbol name)
{
  const auto roundRect = [&p](qreal x, qreal y, qreal w, qreal h, qreal r) {
    p.drawRoundedRect(QRectF(x, y, w, h), r, r);
  };
  const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
  const auto dot = [&p](qreal x, qreal y) {
    const auto pen = p.pen();
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(pen.color());
    p.drawEllipse(QPointF(x, y), 1.1, 1.1);
    p.restore();
  };
  const auto filled = [&p](const QPainterPath &path) {
    p.save();
    p.setBrush(p.pen().color());
    p.drawPath(path);
    p.restore();
  };

  switch (name) {
  case Symbol::Computers:
    roundRect(2.5, 4, 13, 9.5, 1.6);
    line(6.5, 18, 11.5, 18);
    line(9, 13.5, 9, 18);
    roundRect(17.5, 7, 4, 11, 1.2);
    break;
  case Symbol::Remote: {
    // a display with a pointer on it
    roundRect(2.5, 3.5, 19, 13, 2);
    line(9, 20.5, 15, 20.5);
    line(12, 16.5, 12, 20.5);
    QPainterPath arrow;
    arrow.moveTo(10, 6.5);
    arrow.lineTo(15.5, 11);
    arrow.lineTo(12.6, 11.4);
    arrow.lineTo(11.3, 14);
    arrow.closeSubpath();
    p.drawPath(arrow);
    break;
  }
  case Symbol::Clipboard:
    roundRect(5, 4.5, 14, 16.5, 2);
    roundRect(9, 2.5, 6, 4, 1);
    line(9, 11, 15, 11);
    line(9, 15, 13, 15);
    break;
  case Symbol::Screenshot:
    // a selection's corners around a small camera lens
    line(3.5, 8, 3.5, 3.5);
    line(3.5, 3.5, 8, 3.5);
    line(16, 3.5, 20.5, 3.5);
    line(20.5, 3.5, 20.5, 8);
    line(20.5, 16, 20.5, 20.5);
    line(20.5, 20.5, 16, 20.5);
    line(8, 20.5, 3.5, 20.5);
    line(3.5, 20.5, 3.5, 16);
    p.drawEllipse(QPointF(12, 12), 3.6, 3.6);
    break;
  case Symbol::Sparkle: {
    QPainterPath star;
    star.moveTo(11, 3);
    star.quadTo(11.8, 9.2, 18, 10);
    star.quadTo(11.8, 10.8, 11, 17);
    star.quadTo(10.2, 10.8, 4, 10);
    star.quadTo(10.2, 9.2, 11, 3);
    p.drawPath(star);
    line(18.5, 15.5, 18.5, 20.5);
    line(16, 18, 21, 18);
    break;
  }
  case Symbol::Log:
    line(8.5, 6.5, 20, 6.5);
    line(8.5, 12, 20, 12);
    line(8.5, 17.5, 16, 17.5);
    dot(4.5, 6.5);
    dot(4.5, 12);
    dot(4.5, 17.5);
    break;
  case Symbol::Settings:
    line(4, 7, 12.5, 7);
    line(17.5, 7, 20, 7);
    line(4, 17, 6.5, 17);
    line(11.5, 17, 20, 17);
    p.drawEllipse(QPointF(15, 7), 2.3, 2.3);
    p.drawEllipse(QPointF(9, 17), 2.3, 2.3);
    break;
  case Symbol::Play: {
    QPainterPath tri;
    tri.moveTo(8, 5.5);
    tri.lineTo(18.5, 12);
    tri.lineTo(8, 18.5);
    tri.closeSubpath();
    filled(tri);
    break;
  }
  case Symbol::Stop: {
    QPainterPath square;
    square.addRoundedRect(QRectF(6.5, 6.5, 11, 11), 2, 2);
    filled(square);
    break;
  }
  case Symbol::Restart: {
    QPainterPath arc;
    arc.arcMoveTo(QRectF(4, 4, 16, 16), 50);
    arc.arcTo(QRectF(4, 4, 16, 16), 50, 290);
    p.drawPath(arc);
    line(17.2, 3.5, 17.2, 7.8);
    line(17.2, 7.8, 12.9, 7.8);
    break;
  }
  case Symbol::Pencil: {
    QPainterPath pen;
    pen.moveTo(4.5, 19.5);
    pen.lineTo(4.5, 16);
    pen.lineTo(15.5, 5);
    pen.lineTo(19, 8.5);
    pen.lineTo(8, 19.5);
    pen.closeSubpath();
    p.drawPath(pen);
    line(13, 7.5, 16.5, 11);
    break;
  }
  case Symbol::Layout:
    roundRect(2.5, 5, 8.5, 6, 1.3);
    roundRect(13, 5, 8.5, 6, 1.3);
    roundRect(7.75, 13.5, 8.5, 6, 1.3);
    break;
  case Symbol::Keyboard:
    roundRect(2.5, 6, 19, 12, 2.2);
    dot(6.5, 10);
    dot(10, 10);
    dot(13.75, 10);
    dot(17.5, 10);
    line(8, 14.25, 16, 14.25);
    break;
  case Symbol::Cursor: {
    QPainterPath arrow;
    arrow.moveTo(5.5, 3.5);
    arrow.lineTo(17.5, 10.5);
    arrow.lineTo(12, 12);
    arrow.lineTo(9.5, 17.5);
    arrow.closeSubpath();
    p.drawPath(arrow);
    QPainterPath ripple;
    ripple.arcMoveTo(QRectF(9, 9, 13, 13), -60);
    ripple.arcTo(QRectF(9, 9, 13, 13), -60, 90);
    p.drawPath(ripple);
    break;
  }
  case Symbol::Lock: {
    roundRect(5, 10.5, 14, 10, 2.2);
    QPainterPath shackle;
    shackle.moveTo(8, 10.5);
    shackle.lineTo(8, 7.5);
    shackle.arcTo(QRectF(8, 3.5, 8, 8), 180, -180);
    shackle.lineTo(16, 10.5);
    p.drawPath(shackle);
    break;
  }
  case Symbol::Shield: {
    QPainterPath shield;
    shield.moveTo(12, 3);
    shield.lineTo(19.5, 6);
    shield.lineTo(19.5, 11.5);
    shield.cubicTo(19.5, 16, 16.5, 19.2, 12, 21);
    shield.cubicTo(7.5, 19.2, 4.5, 16, 4.5, 11.5);
    shield.lineTo(4.5, 6);
    shield.closeSubpath();
    p.drawPath(shield);
    p.drawPolyline(QPolygonF({{8.8, 12}, {11.2, 14.4}, {15.4, 9.6}}));
    break;
  }
  case Symbol::Files:
    line(12, 15, 12, 4);
    line(7.5, 8.5, 12, 4);
    line(16.5, 8.5, 12, 4);
    p.drawPolyline(QPolygonF({{4, 14}, {4, 18.5}, {5.5, 20}, {18.5, 20}, {20, 18.5}, {20, 14}}));
    break;
  case Symbol::Export:
    line(12, 3.5, 12, 14.5);
    line(7.5, 10, 12, 14.5);
    line(16.5, 10, 12, 14.5);
    p.drawPolyline(QPolygonF({{4, 16.5}, {4, 18.5}, {5.5, 20}, {18.5, 20}, {20, 18.5}, {20, 16.5}}));
    break;
  case Symbol::Server:
    roundRect(2.5, 4, 12, 9, 1.6);
    line(6.5, 17.5, 10.5, 17.5);
    line(8.5, 13, 8.5, 17.5);
    line(16, 8.5, 21.5, 8.5);
    line(19, 6, 21.5, 8.5);
    line(19, 11, 21.5, 8.5);
    break;
  case Symbol::Client:
    roundRect(9.5, 4, 12, 9, 1.6);
    line(13.5, 17.5, 17.5, 17.5);
    line(15.5, 13, 15.5, 17.5);
    line(2.5, 8.5, 8, 8.5);
    line(5.5, 6, 8, 8.5);
    line(5.5, 11, 8, 8.5);
    break;
  }
}

QPixmap renderSymbol(Symbol name, const QColor &color, int size, qreal dpr)
{
  QPixmap pixmap(QSize(size, size) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(size / 24.0, size / 24.0);
  QPen pen(color, size <= 16 ? 2.0 : 1.7);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  drawSymbol(painter, name);
  return pixmap;
}

} // namespace

QIcon symbol(Symbol name, const QColor &color, const QColor &onColor)
{
  QIcon icon;
  for (const int size : {16, 18, 20, 24, 32}) {
    for (const qreal dpr : {1.0, 2.0}) {
      icon.addPixmap(renderSymbol(name, color, size, dpr), QIcon::Normal, QIcon::Off);
      if (onColor.isValid())
        icon.addPixmap(renderSymbol(name, onColor, size, dpr), QIcon::Normal, QIcon::On);
    }
  }
  return icon;
}

QLabel *label(const QString &text, const char *role, QWidget *parent)
{
  auto *l = new QLabel(text, parent);
  if (role)
    l->setProperty(role, true);
  return l;
}

// ---------------------------------------------------------------------------
// ToggleSwitch
// ---------------------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent), m_animation{new QVariantAnimation(this)}
{
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_animation->setDuration(140);
  m_animation->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
    m_position = value.toReal();
    update();
  });
}

QSize ToggleSwitch::sizeHint() const
{
  return {38, 22};
}

void ToggleSwitch::checkStateSet()
{
  // set programmatically: jump, don't animate
  m_animation->stop();
  m_position = isChecked() ? 1 : 0;
  update();
}

void ToggleSwitch::nextCheckState()
{
  QAbstractButton::nextCheckState();
  animateTo(isChecked());
}

void ToggleSwitch::animateTo(bool on)
{
  m_animation->stop();
  m_animation->setStartValue(m_position);
  m_animation->setEndValue(on ? 1.0 : 0.0);
  m_animation->start();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
  const qreal radius = track.height() / 2;

  QColor off = theme::border();
  QColor on = theme::accent();
  QColor fill;
  fill.setRgbF(
      off.redF() + (on.redF() - off.redF()) * m_position, off.greenF() + (on.greenF() - off.greenF()) * m_position,
      off.blueF() + (on.blueF() - off.blueF()) * m_position
  );
  if (!isEnabled())
    fill.setAlphaF(0.45);

  p.setPen(Qt::NoPen);
  p.setBrush(fill);
  p.drawRoundedRect(track, radius, radius);

  const qreal knob = track.height() - 4;
  const qreal x = track.left() + 2 + (track.width() - knob - 4) * m_position;
  p.setBrush(QColor(0, 0, 0, 40));
  p.drawEllipse(QRectF(x, track.top() + 2.6, knob, knob));
  p.setBrush(isEnabled() ? QColor(Qt::white) : QColor(245, 245, 245));
  p.drawEllipse(QRectF(x, track.top() + 2, knob, knob));

  if (hasFocus() && focusPolicy() != Qt::NoFocus) {
    p.setBrush(Qt::NoBrush);
    QColor ring = theme::accent();
    ring.setAlphaF(0.4);
    p.setPen(QPen(ring, 2));
    p.drawRoundedRect(track.adjusted(-0.5, -0.5, 0.5, 0.5), radius, radius);
  }
}

// ---------------------------------------------------------------------------
// SegmentedControl
// ---------------------------------------------------------------------------

SegmentedControl::SegmentedControl(QWidget *parent)
    : QFrame(parent),
      m_layout{new QHBoxLayout(this)},
      m_group{new QButtonGroup(this)}
{
  setProperty("segmentTrack", true);
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  m_layout->setContentsMargins(2, 2, 2, 2);
  m_layout->setSpacing(2);
  m_group->setExclusive(true);
  connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
    if (checked)
      Q_EMIT currentChanged(id);
  });
}

int SegmentedControl::addSegment(const QString &text, const QIcon &icon)
{
  auto *button = new QPushButton(icon, text, this);
  button->setProperty("segment", true);
  button->setCheckable(true);
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::TabFocus);
  const int id = m_group->buttons().size();
  m_group->addButton(button, id);
  m_layout->addWidget(button);
  if (id == 0)
    button->setChecked(true);
  return id;
}

void SegmentedControl::setSegmentText(int index, const QString &text)
{
  if (auto *button = m_group->button(index))
    button->setText(text);
}

void SegmentedControl::setCurrentIndex(int index)
{
  if (auto *button = m_group->button(index))
    button->setChecked(true);
}

int SegmentedControl::currentIndex() const
{
  return m_group->checkedId();
}

int SegmentedControl::count() const
{
  return static_cast<int>(m_group->buttons().size());
}

// ---------------------------------------------------------------------------
// FormCard
// ---------------------------------------------------------------------------

FormCard::FormCard(QWidget *parent) : QFrame(parent), m_layout{new QVBoxLayout(this)}
{
  setProperty("card", true);
  m_layout->setContentsMargins(16, 4, 16, 4);
  m_layout->setSpacing(0);
}

FormCard::Row FormCard::addRow(const QString &title, const QString &detail, QWidget *control)
{
  Row row;
  row.widget = new QWidget(this);
  auto *layout = new QHBoxLayout(row.widget);
  layout->setContentsMargins(0, 10, 0, 10);
  layout->setSpacing(16);

  auto *texts = new QVBoxLayout();
  texts->setSpacing(2);
  row.title = label(title, "rowTitle", row.widget);
  row.title->setWordWrap(true);
  texts->addWidget(row.title);
  row.detail = label(detail, "rowDetail", row.widget);
  row.detail->setWordWrap(true);
  row.detail->setVisible(!detail.isEmpty());
  texts->addWidget(row.detail);
  layout->addLayout(texts, 1);

  if (control) {
    control->setParent(row.widget);
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
  }

  addWidget(row.widget);
  return row;
}

QWidget *FormCard::addWidget(QWidget *widget)
{
  if (!m_rows.isEmpty()) {
    auto *separator = new QFrame(this);
    separator->setProperty("rowSeparator", true);
    m_layout->addWidget(separator);
    m_separators.append(separator);
  } else {
    m_separators.append(nullptr);
  }
  widget->setParent(this);
  m_layout->addWidget(widget);
  m_rows.append(widget);
  return widget;
}

void FormCard::setRowVisible(QWidget *row, bool visible)
{
  row->setVisible(visible);
  updateSeparators();
}

void FormCard::updateSeparators()
{
  // a hairline only goes between two visible rows
  bool seenVisible = false;
  for (qsizetype i = 0; i < m_rows.size(); ++i) {
    const bool visible = !m_rows.at(i)->isHidden();
    if (auto *separator = m_separators.at(i))
      separator->setVisible(visible && seenVisible);
    seenVisible = seenVisible || visible;
  }
}

// ---------------------------------------------------------------------------
// ChoiceCard
// ---------------------------------------------------------------------------

namespace {
constexpr int kChoicePadding = 16;
constexpr int kChoiceIcon = 36;
constexpr int kChoiceGap = 12;
} // namespace

ChoiceCard::ChoiceCard(Symbol symbol, QWidget *parent) : QRadioButton(parent), m_symbol{symbol}
{
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover);
  QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
}

void ChoiceCard::setTitle(const QString &title)
{
  m_title = title;
  setText(title); // for accessibility and keyboard mnemonics
  updateGeometry();
  update();
}

void ChoiceCard::setDescription(const QString &description)
{
  m_description = description;
  setToolTip(QString());
  setAccessibleDescription(description);
  updateGeometry();
  update();
}

QRect ChoiceCard::textRect(int width) const
{
  const int left = kChoicePadding + kChoiceIcon + kChoiceGap;
  return QRect(left, kChoicePadding, qMax(40, width - left - kChoicePadding - 22), 1000);
}

int ChoiceCard::heightForWidth(int width) const
{
  QFont titleFont = font();
  titleFont.setWeight(QFont::DemiBold);
  const QFontMetrics titleMetrics(titleFont);
  const QFontMetrics bodyMetrics(font());
  const auto area = textRect(width);
  const int titleHeight = titleMetrics.boundingRect(area, Qt::TextWordWrap, m_title).height();
  const int bodyHeight = bodyMetrics.boundingRect(area, Qt::TextWordWrap, m_description).height();
  return qMax(kChoiceIcon, titleHeight + 4 + bodyHeight) + 2 * kChoicePadding;
}

bool ChoiceCard::hasHeightForWidth() const
{
  return true;
}

QSize ChoiceCard::sizeHint() const
{
  return {280, heightForWidth(280)};
}

QSize ChoiceCard::minimumSizeHint() const
{
  return {200, heightForWidth(200)};
}

bool ChoiceCard::hitButton(const QPoint &pos) const
{
  return rect().contains(pos);
}

void ChoiceCard::enterEvent(QEnterEvent *event)
{
  m_hover = true;
  update();
  QRadioButton::enterEvent(event);
}

void ChoiceCard::leaveEvent(QEvent *event)
{
  m_hover = false;
  update();
  QRadioButton::leaveEvent(event);
}

void ChoiceCard::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const auto pal = palette();
  const QColor accent = theme::accent();
  const bool checked = isChecked();

  // card
  const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
  QColor fill = pal.color(QPalette::Base);
  if (checked) {
    QColor tint = accent;
    tint.setAlphaF(theme::isDark() ? 0.16 : 0.07);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(card, 14, 14);
    fill = tint;
  }
  p.setPen(QPen(checked ? accent : (m_hover ? pal.color(QPalette::Dark) : pal.color(QPalette::Mid)), checked ? 2 : 1));
  p.setBrush(fill);
  p.drawRoundedRect(checked ? card.adjusted(0.5, 0.5, -0.5, -0.5) : card, 14, 14);

  // symbol in a soft circle
  const QRectF iconCircle(kChoicePadding, kChoicePadding, kChoiceIcon, kChoiceIcon);
  QColor circle = checked ? accent : pal.color(QPalette::Midlight);
  p.setPen(Qt::NoPen);
  p.setBrush(circle);
  p.drawEllipse(iconCircle);
  const auto icon = symbol(m_symbol, checked ? QColor(Qt::white) : theme::textSecondary());
  icon.paint(&p, iconCircle.adjusted(8, 8, -8, -8).toRect());

  // check mark
  const QRectF mark(width() - kChoicePadding - 18, kChoicePadding, 18, 18);
  if (checked) {
    p.setBrush(accent);
    p.drawEllipse(mark);
    QPen tick(Qt::white, 2);
    tick.setCapStyle(Qt::RoundCap);
    tick.setJoinStyle(Qt::RoundJoin);
    p.setPen(tick);
    p.drawPolyline(QPolygonF(
        {mark.topLeft() + QPointF(5, 9.5), mark.topLeft() + QPointF(8, 12.5), mark.topLeft() + QPointF(13.5, 6.5)}
    ));
  } else {
    p.setPen(QPen(pal.color(QPalette::Dark), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(mark.adjusted(0.5, 0.5, -0.5, -0.5));
  }

  // text
  const auto area = textRect(width());
  QFont titleFont = font();
  titleFont.setWeight(QFont::DemiBold);
  p.setFont(titleFont);
  p.setPen(pal.color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
  const QRect titleBounds = p.boundingRect(area, Qt::TextWordWrap, m_title);
  p.drawText(area, Qt::TextWordWrap, m_title);

  p.setFont(font());
  p.setPen(theme::textSecondary());
  p.drawText(area.adjusted(0, titleBounds.height() + 4, 0, 0), Qt::TextWordWrap, m_description);

  if (hasFocus()) {
    QColor ring = accent;
    ring.setAlphaF(0.35);
    p.setPen(QPen(ring, 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(-0.5, -0.5, 0.5, 0.5), 15, 15);
  }
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

Page::Page(QWidget *parent)
    : QWidget(parent),
      m_scroll{new QScrollArea(this)},
      m_title{label(QString(), "title", this)},
      m_subtitle{label(QString(), "secondary", this)},
      m_titleRow{new QHBoxLayout()},
      m_content{new QVBoxLayout()}
{
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(m_scroll);

  m_scroll->setWidgetResizable(true);
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *body = new QWidget(m_scroll);
  body->setProperty("page", true);
  auto *bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(32, 28, 32, 28);
  bodyLayout->addStretch(1);

  auto *column = new QWidget(body);
  column->setMaximumWidth(720);
  column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto *columnLayout = new QVBoxLayout(column);
  columnLayout->setContentsMargins(0, 0, 0, 0);
  columnLayout->setSpacing(6);

  m_titleRow->setSpacing(8);
  m_titleRow->addWidget(m_title);
  m_titleRow->addStretch(1);
  columnLayout->addLayout(m_titleRow);

  m_subtitle->setWordWrap(true);
  m_subtitle->setVisible(false);
  columnLayout->addWidget(m_subtitle);
  columnLayout->addSpacing(14);

  m_content->setSpacing(10);
  columnLayout->addLayout(m_content);
  columnLayout->addStretch(1);

  bodyLayout->addWidget(column, 100);
  bodyLayout->addStretch(1);
  m_scroll->setWidget(body);
}

void Page::setTitle(const QString &title)
{
  m_title->setText(title);
}

void Page::setSubtitle(const QString &subtitle)
{
  m_subtitle->setText(subtitle);
  m_subtitle->setVisible(!subtitle.isEmpty());
}

void Page::setTrailing(QWidget *widget)
{
  m_titleRow->addWidget(widget, 0, Qt::AlignVCenter);
}

QLabel *Page::addSection(const QString &title)
{
  auto *section = label(title, "eyebrow");
  if (m_content->count() > 0)
    m_content->addSpacing(8);
  m_content->addWidget(section);
  return section;
}

void Page::addWidget(QWidget *widget)
{
  m_content->addWidget(widget);
}

void Page::addLayout(QLayout *layout)
{
  m_content->addLayout(layout);
}

void Page::addStretch()
{
  m_content->addStretch(1);
}

} // namespace deskflow::gui::mac
