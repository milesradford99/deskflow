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
constexpr auto kMaxEntries = 6;
constexpr auto kMaxTextLength = 40;
constexpr qint64 kKeyLifetime = 1800;
constexpr qint64 kActivityLifetime = 3500;
constexpr qint64 kJoinWindow = 1500;
constexpr qint64 kRepeatWindow = 1000;
constexpr qint64 kFadeIn = 120;
constexpr qint64 kFadeOut = 400;
constexpr auto kKeyPixelSize = 28;
constexpr auto kActivityPixelSize = 18;
constexpr auto kPadding = 14;
constexpr auto kSpacing = 8;
constexpr auto kRadius = 12;
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
  // Short glyph keys (↵, ⌫, arrows) read naturally inline with typed text.
  const bool joinable = isText || label.size() == 1;

  if (!m_entries.isEmpty()) {
    auto &last = m_entries.last();
    if (last.kind == Kind::Keys && t - last.lastUpdate < kJoinWindow) {
      if (joinable && last.joinable) {
        last.text += label;
        if (last.text.size() > kMaxTextLength)
          last.text = QStringLiteral("…") + last.text.right(kMaxTextLength - 1);
        last.lastUpdate = t;
        update();
        return;
      }
      if (!joinable && !last.joinable && last.text == label && t - last.lastUpdate < kRepeatWindow) {
        ++last.repeat;
        last.lastUpdate = t;
        update();
        return;
      }
    }
  }

  Entry entry;
  entry.kind = Kind::Keys;
  entry.text = label;
  entry.joinable = joinable;
  entry.lifetime = kKeyLifetime;
  push(entry);
}

void ActivityOverlay::addActivity(Kind kind, const QString &text)
{
  if (!m_showActivity || text.isEmpty())
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
  m_entries.append(entry);
  while (m_entries.size() > kMaxEntries)
    m_entries.removeFirst();

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
  const int width = qMin(900, area.width() * 4 / 5);
  const int height = kMaxEntries * (kKeyPixelSize * 2 + kSpacing);
  const int x = area.x() + (area.width() - width) / 2;
  const int y = area.y() + area.height() - height - area.height() / 10;
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
  int bottom = height();

  // newest at the bottom, older ones stacked above
  for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
    const auto &entry = *it;
    const bool isKeys = entry.kind == Kind::Keys;

    qreal opacity = 1.0;
    const auto age = t - entry.created;
    const auto remaining = entry.lifetime - (t - entry.lastUpdate);
    if (age < kFadeIn)
      opacity = qreal(age) / kFadeIn;
    if (remaining < kFadeOut)
      opacity = qMin(opacity, qMax<qreal>(0, qreal(remaining) / kFadeOut));

    QString text = entry.text;
    if (entry.repeat > 1 && !entry.joinable)
      text += QStringLiteral("  ×%1").arg(entry.repeat);

    const QFont &textFont = isKeys ? keyFont : activityFont;
    const QFontMetrics fm(textFont);
    const QString glyph = glyphFor(entry.kind);
    const int glyphWidth = glyph.isEmpty() ? 0 : fm.horizontalAdvance(glyph) + kPadding / 2;
    const int maxTextWidth = width() - 2 * kPadding - glyphWidth;
    text = fm.elidedText(text, Qt::ElideLeft, maxTextWidth);

    const int bubbleWidth = fm.horizontalAdvance(text) + glyphWidth + 2 * kPadding;
    const int bubbleHeight = fm.height() + kPadding;
    const QRect bubble((width() - bubbleWidth) / 2, bottom - bubbleHeight, bubbleWidth, bubbleHeight);
    if (bubble.top() < 0)
      break;

    painter.setOpacity(opacity);
    QPainterPath path;
    path.addRoundedRect(bubble, kRadius, kRadius);
    painter.fillPath(path, QColor(0, 0, 0, kBackgroundAlpha));

    int textX = bubble.left() + kPadding;
    if (!glyph.isEmpty()) {
      painter.setFont(textFont);
      painter.setPen(accentFor(entry.kind));
      painter.drawText(
          QRect(textX, bubble.top(), glyphWidth, bubble.height()), Qt::AlignLeft | Qt::AlignVCenter, glyph
      );
      textX += glyphWidth;
    }

    painter.setFont(textFont);
    painter.setPen(Qt::white);
    painter.drawText(
        QRect(textX, bubble.top(), bubble.right() - textX - kPadding + 1, bubble.height()),
        Qt::AlignLeft | Qt::AlignVCenter, text
    );

    bottom = bubble.top() - kSpacing;
  }
}
