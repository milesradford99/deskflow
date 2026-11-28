/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ActivityOverlay.h"

#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

namespace {
// Keys are a single row of small bubbles, one per press, newest on the right.
// Activity events stack underneath it. Everything is anchored to the top-right corner.
constexpr auto kMaxKeyEntries = 8;
constexpr auto kMaxActivityEntries = 4;
constexpr qint64 kKeyLifetime = 1100;
constexpr qint64 kActivityLifetime = 3500;
constexpr qint64 kRepeatWindow = 800;
constexpr qint64 kFadeIn = 80;
constexpr qint64 kFadeOut = 300;
constexpr auto kKeyPixelSize = 22;
constexpr auto kActivityPixelSize = 16;
constexpr auto kPadding = 12;
constexpr auto kSpacing = 6;
constexpr auto kRadius = 10;
constexpr auto kScreenMargin = 24;
constexpr auto kBackgroundAlpha = 190;
} // namespace

using deskflow::core::ConnectionState;

ActivityOverlay::ActivityOverlay(QWidget *parent)
    : QWidget(
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                      Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint
      ),
      m_timer{new QTimer(this)}
{
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  // Tool windows are hidden on macOS when the app is not active, which is nearly always for deskflow.
  setAttribute(Qt::WA_MacAlwaysShowToolWindow);
  setFocusPolicy(Qt::NoFocus);
  setWindowTitle(QStringLiteral("Deskflow activity"));

  m_timer->setInterval(16);
  connect(m_timer, &QTimer::timeout, this, &ActivityOverlay::tick);
}

void ActivityOverlay::setShowKeys(bool show)
{
  m_showKeys = show;
  if (!show) {
    m_entries.removeIf([](const Entry &e) { return e.kind == Kind::Keys; });
    update();
  }
}

void ActivityOverlay::setShowActivity(bool show)
{
  m_showActivity = show;
  if (!show) {
    m_entries.removeIf([](const Entry &e) { return e.kind != Kind::Keys; });
    update();
  }
}

void ActivityOverlay::addKey(const QString &label, bool isText)
{
  if (!m_showKeys || label.isEmpty())
    return;

  const auto t = now();

  // The same shortcut pressed again quickly (Cmd+Z, Cmd+Z...) counts up instead of
  // filling the row. Typed characters are never merged: each one is its own bubble.
  for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
    if (it->kind != Kind::Keys)
      continue;
    if (!isText && !it->isText && it->text == label && t - it->lastUpdate < kRepeatWindow) {
      ++it->repeat;
      it->lastUpdate = t;
      update();
      return;
    }
    break;
  }

  Entry entry;
  entry.kind = Kind::Keys;
  entry.text = label;
  entry.isText = isText;
  entry.lifetime = kKeyLifetime;
  push(entry);
}

void ActivityOverlay::addActivity(Kind kind, const QString &text, bool always)
{
  if ((!m_showActivity && !always) || text.isEmpty())
    return;

  // The same event twice in a row (e.g. clipboard updates) just refreshes the bubble.
  if (!m_entries.isEmpty()) {
    auto &last = m_entries.last();
    if (last.kind == kind && last.text == text) {
      last.lastUpdate = now();
      ++last.repeat;
      update();
      return;
    }
  }

  Entry entry;
  entry.kind = kind;
  entry.text = text;
  entry.lifetime = kActivityLifetime;
  push(entry);
}

void ActivityOverlay::handleCoreActivity(const QString &type, const QStringList &fields)
{
  const auto field = [&fields](int i) { return i < fields.size() ? fields.at(i) : QString(); };

  if (type == QStringLiteral("switched")) {
    addActivity(Kind::Switch, tr("Control: %1 → %2").arg(field(0), field(1)));
  } else if (type == QStringLiteral("entered")) {
    addActivity(Kind::Switch, tr("Control moved to this computer"));
  } else if (type == QStringLiteral("left")) {
    addActivity(Kind::Switch, tr("Control left this computer"));
  } else if (type == QStringLiteral("clipboardShared")) {
    addActivity(Kind::Clipboard, tr("Clipboard from %1 shared").arg(field(0)));
  } else if (type == QStringLiteral("clipboardReceived")) {
    addActivity(Kind::Clipboard, tr("Clipboard received"));
  } else if (type == QStringLiteral("clipboardSent")) {
    addActivity(Kind::Clipboard, tr("Clipboard sent"));
  } else if (type == QStringLiteral("cursorLock")) {
    if (field(0) == QStringLiteral("on"))
      addActivity(Kind::Info, tr("Cursor locked to %1").arg(field(1)));
    else
      addActivity(Kind::Info, tr("Cursor unlocked"));
  } else if (type == QStringLiteral("remoteViewStarted")) {
    // shown even with screen hints off: people should know when their screen is being watched
    addActivity(Kind::Info, tr("%1 is viewing this screen").arg(field(0)), true);
  } else if (type == QStringLiteral("remoteViewStopped")) {
    addActivity(Kind::Info, tr("%1 stopped viewing this screen").arg(field(0)), true);
  } else if (type == QStringLiteral("keyboardBroadcast")) {
    if (field(0) == QStringLiteral("on"))
      addActivity(Kind::Info, tr("Keyboard broadcast on"));
    else
      addActivity(Kind::Info, tr("Keyboard broadcast off"));
  }
}

void ActivityOverlay::handleServerClients(const QStringList &clients)
{
  const QSet<QString> current(clients.cbegin(), clients.cend());

  // The first list after start is the baseline, not a burst of "connected" bubbles.
  if (m_clientsKnown) {
    for (const auto &name : current) {
      if (!m_knownClients.contains(name))
        addActivity(Kind::Connected, tr("%1 connected").arg(name));
    }
    for (const auto &name : std::as_const(m_knownClients)) {
      if (!current.contains(name))
        addActivity(Kind::Disconnected, tr("%1 disconnected").arg(name));
    }
  }

  m_knownClients = current;
  m_clientsKnown = true;
}

void ActivityOverlay::handleClientConnectionState(ConnectionState state)
{
  if (state == m_clientState)
    return;

  if (state == ConnectionState::Connected)
    addActivity(Kind::Connected, tr("Connected to server"));
  else if (m_clientState == ConnectionState::Connected)
    addActivity(Kind::Disconnected, tr("Disconnected from server"));

  m_clientState = state;
}

void ActivityOverlay::resetConnectionTracking()
{
  m_knownClients.clear();
  m_clientsKnown = false;
  m_clientState = ConnectionState::Disconnected;
}

void ActivityOverlay::clear()
{
  m_entries.clear();
  m_timer->stop();
  hide();
}

void ActivityOverlay::push(Entry entry)
{
  const auto t = now();
  entry.created = t;
  entry.lastUpdate = t;
  const bool isKeys = entry.kind == Kind::Keys;
  m_entries.append(entry);

  // Drop the oldest entries of the same group (keys or activity) beyond its limit.
  const int limit = isKeys ? kMaxKeyEntries : kMaxActivityEntries;
  int count = 0;
  for (const auto &e : std::as_const(m_entries)) {
    if ((e.kind == Kind::Keys) == isKeys)
      ++count;
  }
  for (auto it = m_entries.begin(); count > limit && it != m_entries.end();) {
    if ((it->kind == Kind::Keys) == isKeys) {
      it = m_entries.erase(it);
      --count;
    } else {
      ++it;
    }
  }

  if (!isVisible()) {
    reposition();
    show();
  }
  if (!m_timer->isActive())
    m_timer->start();
  update();
}

void ActivityOverlay::tick()
{
  const auto t = now();
  m_entries.removeIf([t](const Entry &e) { return t - e.lastUpdate > e.lifetime; });
  if (m_entries.isEmpty()) {
    m_timer->stop();
    hide();
    return;
  }
  update();
}

void ActivityOverlay::reposition()
{
  // Follow the cursor: on a client that's where the user is looking, on the server it's
  // the screen they were last on.
  QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  if (!screen)
    return;

  const auto area = screen->availableGeometry();
  const int width = qMin(720, area.width() * 3 / 5);
  const int height = (kKeyPixelSize * 2 + kSpacing) + kMaxActivityEntries * (kActivityPixelSize * 2 + kSpacing);
  const int x = area.x() + area.width() - width - kScreenMargin;
  const int y = area.y() + kScreenMargin;
  setGeometry(x, y, width, height);
}

qint64 ActivityOverlay::now() const
{
  return QDateTime::currentMSecsSinceEpoch();
}

QColor ActivityOverlay::accentFor(Kind kind)
{
  switch (kind) {
  case Kind::Switch:
    return QColor(0x4d, 0xa3, 0xff);
  case Kind::Clipboard:
    return QColor(0xb3, 0x8c, 0xff);
  case Kind::Connected:
    return QColor(0x3d, 0xd6, 0x8c);
  case Kind::Disconnected:
    return QColor(0xff, 0x6b, 0x6b);
  case Kind::Info:
    return QColor(0xff, 0xc1, 0x4d);
  case Kind::Keys:
  default:
    return {};
  }
}

QString ActivityOverlay::glyphFor(Kind kind)
{
  switch (kind) {
  case Kind::Switch:
    return QStringLiteral("⇄");
  case Kind::Clipboard:
    return QStringLiteral("⎘");
  case Kind::Connected:
    return QStringLiteral("●");
  case Kind::Disconnected:
    return QStringLiteral("○");
  case Kind::Info:
    return QStringLiteral("•");
  case Kind::Keys:
  default:
    return {};
  }
}

qreal ActivityOverlay::opacityFor(const Entry &entry, qint64 t)
{
  qreal opacity = 1.0;
  const auto age = t - entry.created;
  const auto remaining = entry.lifetime - (t - entry.lastUpdate);
  if (age < kFadeIn)
    opacity = qreal(age) / kFadeIn;
  if (remaining < kFadeOut)
    opacity = qMin(opacity, qMax<qreal>(0, qreal(remaining) / kFadeOut));
  return opacity;
}

void ActivityOverlay::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  QFont keyFont = font();
  keyFont.setPixelSize(kKeyPixelSize);
  keyFont.setWeight(QFont::DemiBold);
  QFont activityFont = font();
  activityFont.setPixelSize(kActivityPixelSize);

  const auto t = now();

  const auto drawBubble = [&painter](const QRect &bubble, qreal opacity) {
    painter.setOpacity(opacity);
    QPainterPath path;
    path.addRoundedRect(bubble, kRadius, kRadius);
    painter.fillPath(path, QColor(0, 0, 0, kBackgroundAlpha));
  };

  // Row of key bubbles along the top edge, newest at the right.
  const QFontMetrics keyMetrics(keyFont);
  const int keyHeight = keyMetrics.height() + kPadding;
  int right = width();
  bool hasKeys = false;
  painter.setFont(keyFont);
  for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
    if (it->kind != Kind::Keys)
      continue;
    hasKeys = true;

    QString text = it->text;
    if (it->repeat > 1)
      text += QStringLiteral(" ×%1").arg(it->repeat);
    text = keyMetrics.elidedText(text, Qt::ElideLeft, width() - 2 * kPadding);

    const int bubbleWidth = qMax(keyHeight, keyMetrics.horizontalAdvance(text) + 2 * kPadding);
    const QRect bubble(right - bubbleWidth, 0, bubbleWidth, keyHeight);
    if (bubble.left() < 0)
      break;

    drawBubble(bubble, opacityFor(*it, t));
    painter.setPen(Qt::white);
    painter.drawText(bubble, Qt::AlignCenter, text);
    right = bubble.left() - kSpacing;
  }

  // Activity events underneath, newest first, right-aligned.
  const QFontMetrics activityMetrics(activityFont);
  int top = hasKeys ? keyHeight + kSpacing : 0;
  painter.setFont(activityFont);
  for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
    if (it->kind == Kind::Keys)
      continue;

    QString text = it->text;
    if (it->repeat > 1)
      text += QStringLiteral("  ×%1").arg(it->repeat);

    const QString glyph = glyphFor(it->kind);
    const int glyphWidth = glyph.isEmpty() ? 0 : activityMetrics.horizontalAdvance(glyph) + kPadding / 2;
    text = activityMetrics.elidedText(text, Qt::ElideRight, width() - 2 * kPadding - glyphWidth);

    const int bubbleWidth = activityMetrics.horizontalAdvance(text) + glyphWidth + 2 * kPadding;
    const int bubbleHeight = activityMetrics.height() + kPadding;
    const QRect bubble(width() - bubbleWidth, top, bubbleWidth, bubbleHeight);
    if (bubble.bottom() > height())
      break;

    drawBubble(bubble, opacityFor(*it, t));

    int textX = bubble.left() + kPadding;
    if (!glyph.isEmpty()) {
      painter.setPen(accentFor(it->kind));
      painter.drawText(
          QRect(textX, bubble.top(), glyphWidth, bubble.height()), Qt::AlignLeft | Qt::AlignVCenter, glyph
      );
      textX += glyphWidth;
    }

    painter.setPen(Qt::white);
    painter.drawText(
        QRect(textX, bubble.top(), bubble.right() - textX - kPadding + 1, bubble.height()),
        Qt::AlignLeft | Qt::AlignVCenter, text
    );

    top = bubble.bottom() + 1 + kSpacing;
  }
}
