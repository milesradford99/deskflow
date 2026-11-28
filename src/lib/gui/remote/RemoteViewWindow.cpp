/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RemoteViewWindow.h"

#include "common/IpcPayload.h"
#include "common/Settings.h"
#include "gui/Theme.h"
#include "gui/widgets/MacControls.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace deskflow::gui::remote {

using namespace deskflow::remote;

namespace {

constexpr int kMoveIntervalMs = 8; // ~120 moves a second at most
constexpr qint64 kKeyRequestIntervalMs = 1000;

QColor canvasColor()
{
  return theme::isDark() ? QColor(0x0e, 0x0e, 0x10) : QColor(0x24, 0x24, 0x27);
}

} // namespace

// --- canvas -----------------------------------------------------------------

RemoteViewCanvas::RemoteViewCanvas(QWidget *parent) : QWidget(parent)
{
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, false); // keys go to the client raw; its own input method composes
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(320, 200);
  m_moveTimer.setSingleShot(true);
  m_moveTimer.setInterval(kMoveIntervalMs);
  connect(&m_moveTimer, &QTimer::timeout, this, &RemoteViewCanvas::flushMove);
}

void RemoteViewCanvas::setPicture(const QImage &picture, const QRect &source)
{
  m_picture = picture;
  m_source = source.isEmpty() ? QRect(QPoint(0, 0), picture.size()) : source;
  update();
}

void RemoteViewCanvas::clearPicture()
{
  m_picture = QImage();
  update();
}

void RemoteViewCanvas::setNotice(const QString &title, const QString &detail)
{
  m_noticeTitle = title;
  m_noticeDetail = detail;
  update();
}

void RemoteViewCanvas::setInteractive(bool interactive)
{
  if (m_interactive == interactive)
    return;
  m_interactive = interactive;
  if (!interactive && m_focused)
    Q_EMIT input({{"t", "leave"}});
  update();
}

QRectF RemoteViewCanvas::pictureRect() const
{
  if (m_picture.isNull())
    return {};
  const QSizeF size = QSizeF(m_picture.size()).scaled(QSizeF(this->size()), Qt::KeepAspectRatio);
  return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}

bool RemoteViewCanvas::toRemote(const QPointF &position, QPoint &remote) const
{
  const QRectF rect = pictureRect();
  if (rect.isEmpty() || !rect.contains(position))
    return false;
  const double fx = (position.x() - rect.x()) / rect.width();
  const double fy = (position.y() - rect.y()) / rect.height();
  remote = QPoint(
      m_source.x() + std::clamp(static_cast<int>(fx * m_source.width()), 0, m_source.width() - 1),
      m_source.y() + std::clamp(static_cast<int>(fy * m_source.height()), 0, m_source.height() - 1)
  );
  return true;
}

void RemoteViewCanvas::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.fillRect(rect(), canvasColor());

  if (!m_picture.isNull()) {
    const QRectF target = pictureRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, target.width() < m_picture.width());
    p.drawImage(target, m_picture);
    if (m_focused && m_interactive) {
      // a quiet accent edge says the keyboard is going to the other computer
      QPen pen(theme::accent(), 2);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(target.adjusted(1, 1, -1, -1));
    }
  }

  if (m_noticeTitle.isEmpty())
    return;

  // a rounded glass card in the middle
  p.setRenderHint(QPainter::Antialiasing);
  QFont titleFont = font();
  titleFont.setPointSizeF(font().pointSizeF() * 1.25);
  titleFont.setWeight(QFont::DemiBold);
  QFont detailFont = font();
  const int cardWidth = std::min(width() - 48, 440);
  const QFontMetrics titleMetrics(titleFont);
  const QFontMetrics detailMetrics(detailFont);
  const QRect titleBounds =
      titleMetrics.boundingRect(QRect(0, 0, cardWidth - 48, 1000), Qt::AlignHCenter | Qt::TextWordWrap, m_noticeTitle);
  const QRect detailBounds = m_noticeDetail.isEmpty() ? QRect()
                                                      : detailMetrics.boundingRect(
                                                            QRect(0, 0, cardWidth - 48, 1000),
                                                            Qt::AlignHCenter | Qt::TextWordWrap, m_noticeDetail
                                                        );
  const int cardHeight = 28 + titleBounds.height() + (detailBounds.isEmpty() ? 0 : 8 + detailBounds.height()) + 28;
  const QRect card((width() - cardWidth) / 2, (height() - cardHeight) / 2, cardWidth, cardHeight);
  QPainterPath path;
  path.addRoundedRect(card, 16, 16);
  p.fillPath(path, QColor(28, 28, 32, m_picture.isNull() ? 255 : 225));
  p.setPen(QColor(255, 255, 255, 30));
  p.drawPath(path);

  p.setPen(Qt::white);
  p.setFont(titleFont);
  QRect titleRect(card.x() + 24, card.y() + 28, cardWidth - 48, titleBounds.height());
  p.drawText(titleRect, Qt::AlignHCenter | Qt::TextWordWrap, m_noticeTitle);
  if (!detailBounds.isEmpty()) {
    p.setPen(QColor(255, 255, 255, 170));
    p.setFont(detailFont);
    p.drawText(
        QRect(card.x() + 24, titleRect.bottom() + 9, cardWidth - 48, detailBounds.height()),
        Qt::AlignHCenter | Qt::TextWordWrap, m_noticeDetail
    );
  }
}

bool RemoteViewCanvas::event(QEvent *event)
{
  // while the viewer has the keyboard, the app's own shortcuts (Cmd+W, Cmd+Q, ...) belong to the client
  if (event->type() == QEvent::ShortcutOverride && m_interactive && hasPicture()) {
    event->accept();
    return true;
  }
  return QWidget::event(event);
}

bool RemoteViewCanvas::focusNextPrevChild(bool)
{
  return false; // Tab is a key for the client, not focus navigation
}

void RemoteViewCanvas::flushMove()
{
  if (!m_movePending)
    return;
  m_movePending = false;
  Q_EMIT input({{"t", "move"}, {"x", m_pendingMove.x()}, {"y", m_pendingMove.y()}});
}

void RemoteViewCanvas::mouseMoveEvent(QMouseEvent *event)
{
  QPoint remote;
  if (!m_interactive || !isActiveWindow() || !toRemote(event->position(), remote))
    return;
  m_lastRemote = remote;
  m_hasLast = true;
  m_pendingMove = remote;
  m_movePending = true;
  if (!m_moveTimer.isActive())
    m_moveTimer.start();
}

void RemoteViewCanvas::mousePressEvent(QMouseEvent *event)
{
  setFocus(Qt::MouseFocusReason);
  QPoint remote;
  if (!m_interactive || !toRemote(event->position(), remote))
    return;
  flushMove();
  m_lastRemote = remote;
  m_hasLast = true;
  Q_EMIT input({{"t", "down"}, {"b", static_cast<int>(event->button())}, {"x", remote.x()}, {"y", remote.y()}});
}

void RemoteViewCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
  mousePressEvent(event); // the client works out double clicks from the timing itself
}

void RemoteViewCanvas::mouseReleaseEvent(QMouseEvent *event)
{
  if (!m_interactive)
    return;
  flushMove();
  QPoint remote = m_lastRemote;
  toRemote(event->position(), remote);
  Q_EMIT input({{"t", "up"}, {"b", static_cast<int>(event->button())}, {"x", remote.x()}, {"y", remote.y()}});
}

void RemoteViewCanvas::wheelEvent(QWheelEvent *event)
{
  QPoint remote;
  if (!m_interactive || !isActiveWindow() || !toRemote(event->position(), remote))
    return;
  event->accept();
  // trackpads send many small steps: pass them on as they are, clients scale by 120 per notch
  QPointF delta = QPointF(event->angleDelta()) + m_wheelRemainder;
  const QPoint whole(static_cast<int>(delta.x()), static_cast<int>(delta.y()));
  m_wheelRemainder = delta - whole;
  if (whole.isNull())
    return;
  flushMove();
  Q_EMIT input({{"t", "wheel"}, {"dx", whole.x()}, {"dy", whole.y()}, {"x", remote.x()}, {"y", remote.y()}});
}

void RemoteViewCanvas::sendKey(QKeyEvent *event, bool down)
{
  if (!m_interactive || !hasPicture())
    return;
  flushMove();
  const QPoint at = m_hasLast ? m_lastRemote : m_source.center();
  Q_EMIT input(
      {{"t", "key"},
       {"down", down},
       {"repeat", event->isAutoRepeat()},
       {"key", event->key()},
       {"text", event->text()},
       {"mods", static_cast<int>(event->modifiers())},
       {"code", static_cast<int>(event->nativeScanCode() != 0 ? event->nativeScanCode() : event->nativeVirtualKey())},
       {"x", at.x()},
       {"y", at.y()}}
  );
}

void RemoteViewCanvas::keyPressEvent(QKeyEvent *event)
{
  event->accept();
  sendKey(event, true);
}

void RemoteViewCanvas::keyReleaseEvent(QKeyEvent *event)
{
  event->accept();
  sendKey(event, false);
}

void RemoteViewCanvas::focusInEvent(QFocusEvent *event)
{
  m_focused = true;
  update();
  QWidget::focusInEvent(event);
}

void RemoteViewCanvas::focusOutEvent(QFocusEvent *event)
{
  m_focused = false;
  m_movePending = false;
  // let go of the client: keys held here come up there, and our cursor's computer gets its clipboard back
  Q_EMIT input({{"t", "leave"}});
  update();
  QWidget::focusOutEvent(event);
}

// --- window -----------------------------------------------------------------

RemoteViewWindow::RemoteViewWindow(const QString &client, QWidget *parent)
    : QWidget(parent, Qt::Window),
      m_client{client}
{
  setAttribute(Qt::WA_DeleteOnClose);
  resize(1200, 780);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *bar = new QWidget(this);
  auto *row = new QHBoxLayout(bar);
  row->setContentsMargins(16, 10, 12, 10);
  row->setSpacing(10);
  m_dot = new QLabel(bar);
  m_dot->setFixedSize(8, 8);
  m_dot->setProperty("dot", true);
  row->addWidget(m_dot, 0, Qt::AlignVCenter);
  auto *text = new QVBoxLayout();
  text->setSpacing(0);
  m_title = mac::label(client, "rowTitle", bar);
  m_status = mac::label(QString(), "tertiary", bar);
  text->addWidget(m_title);
  text->addWidget(m_status);
  row->addLayout(text, 1);

  m_displays = new mac::SegmentedControl(bar);
  m_displays->setVisible(false);
  connect(m_displays, &mac::SegmentedControl::currentChanged, this, [this](int index) {
    if (index == m_display)
      return;
    m_display = index;
    askAgain(true);
  });
  row->addWidget(m_displays);

  m_quality = new mac::SegmentedControl(bar);
  for (int i = 0; i < 3; ++i)
    m_quality->addSegment(QString());
  m_quality->setCurrentIndex(std::clamp(Settings::value(Settings::RemoteView::Quality).toInt(), 0, 2));
  connect(m_quality, &mac::SegmentedControl::currentChanged, this, [this](int index) {
    Settings::setValue(Settings::RemoteView::Quality, index);
    askAgain(true);
  });
  row->addWidget(m_quality);

  m_fullScreen = new QPushButton(bar);
  connect(m_fullScreen, &QPushButton::clicked, this, [this] {
    setWindowState(windowState() ^ Qt::WindowFullScreen);
    m_canvas->setFocus();
  });
  row->addWidget(m_fullScreen);
  layout->addWidget(bar);

  m_canvas = new RemoteViewCanvas(this);
  layout->addWidget(m_canvas, 1);
  connect(m_canvas, &RemoteViewCanvas::input, this, [this](const QJsonObject &event) {
    if (!m_connected)
      return;
    QJsonObject payload = event;
    payload["client"] = m_client;
    Q_EMIT command(deskflow::ipc::kRemoteInput, payload);
  });

  m_fpsClock.start();
  retranslate();
  updateStatus();
}

QJsonObject RemoteViewWindow::request(bool keyFrame) const
{
  QJsonArray codecs;
  if (FrameCanvas::canDecodeH264())
    codecs.append(QStringLiteral("h264"));
  codecs.append(QStringLiteral("tiles"));
  return {
      {"client", m_client},
      {"on", true},
      {"display", m_display},
      {"quality", m_quality->currentIndex()},
      {"fps", Settings::value(Settings::RemoteView::FrameRate).toInt()},
      {"codecs", codecs},
      {"key", keyFrame},
  };
}

void RemoteViewWindow::askAgain(bool keyFrame)
{
  if (!m_connected)
    return;
  m_lastKeyRequest.restart();
  Q_EMIT command(deskflow::ipc::kRemoteView, request(keyFrame));
}

void RemoteViewWindow::handlePiece(const QByteArray &piece)
{
  if (!m_assembler.add(piece))
    return;
  const auto &header = m_assembler.header();
  // acknowledged even if it can't be shown, so the client keeps streaming
  Q_EMIT command(deskflow::ipc::kRemoteAck, {{"client", m_client}, {"sequence", static_cast<qint64>(header.sequence)}});

  if (!m_frames.apply(header, m_assembler.tiles())) {
    if (!m_lastKeyRequest.isValid() || m_lastKeyRequest.elapsed() > kKeyRequestIntervalMs)
      askAgain(true); // a frame went missing or didn't decode: start again from a key frame
    return;
  }
  if (header.displayCount != m_displayCount)
    rebuildDisplays(header.displayCount);
  m_canvas->setPicture(m_frames.image(), header.source);

  ++m_framesThisSecond;
  if (m_fpsClock.elapsed() >= 1000) {
    m_fps = static_cast<int>(std::lround(m_framesThisSecond * 1000.0 / m_fpsClock.restart()));
    m_framesThisSecond = 0;
    m_frameSize = header.size;
    updateStatus();
  } else if (m_frameSize != header.size) {
    m_frameSize = header.size;
    updateStatus();
  }
}

void RemoteViewWindow::handleInfo(const QJsonObject &info)
{
  m_state = info.value("state").toString();
  m_detail = info.value("detail").toString();
  if (info.contains("codec") && !info.value("codec").toString().isEmpty())
    m_codec = info.value("codec").toString();
  if (const auto displays = info.value("displays").toArray(); !displays.isEmpty())
    rebuildDisplays(static_cast<int>(displays.size()));
  updateStatus();
}

void RemoteViewWindow::setConnected(bool connected)
{
  if (m_connected == connected)
    return;
  m_connected = connected;
  if (connected) {
    m_state = QStringLiteral("connecting");
    askAgain(true);
  } else {
    m_state = QStringLiteral("disconnected");
  }
  updateStatus();
}

void RemoteViewWindow::rebuildDisplays(int count)
{
  count = std::max(1, count);
  if (count == m_displayCount && m_displays->count() == (count > 1 ? count : 0))
    return;
  m_displayCount = count;
  const QSignalBlocker blocker(m_displays);
  while (m_displays->count() > 0) {
    // SegmentedControl can't remove segments: replace it
    auto *old = m_displays;
    m_displays = new mac::SegmentedControl(old->parentWidget());
    static_cast<QHBoxLayout *>(old->parentWidget()->layout())->replaceWidget(old, m_displays);
    old->deleteLater();
    connect(m_displays, &mac::SegmentedControl::currentChanged, this, [this](int index) {
      if (index == m_display)
        return;
      m_display = index;
      askAgain(true);
    });
  }
  if (count > 1) {
    for (int i = 0; i < count; ++i)
      m_displays->addSegment(tr("Display %1").arg(i + 1));
    m_displays->setCurrentIndex(std::clamp(m_display, 0, count - 1));
  }
  m_displays->setVisible(count > 1);
}

void RemoteViewWindow::updateStatus()
{
  QString state = QStringLiteral("off");
  QString status;
  QString noticeTitle;
  QString noticeDetail;
  bool interactive = true;

  if (!m_connected || m_state == QStringLiteral("disconnected") || m_state == QStringLiteral("stopped")) {
    status = tr("Not connected");
    noticeTitle = tr("%1 is not connected").arg(m_client);
    noticeDetail = tr("The picture comes back as soon as it connects again.");
    interactive = false;
  } else if (m_state == QStringLiteral("streaming") || m_state == QStringLiteral("controlled")) {
    state = QStringLiteral("ok");
    QStringList parts{tr("Live")};
    if (!m_codec.isEmpty())
      parts << m_codec;
    if (m_frameSize.isValid())
      parts << QStringLiteral("%1 × %2").arg(m_frameSize.width()).arg(m_frameSize.height());
    if (m_fps > 0)
      parts << tr("%1 fps").arg(m_fps);
    status = parts.join(QStringLiteral(" · "));
    if (m_state == QStringLiteral("controlled")) {
      state = QStringLiteral("busy");
      status = tr("Watching only: another computer is in control");
      noticeTitle = tr("Another computer is controlling %1").arg(m_client);
      noticeDetail = tr("Click the picture to take over once it has been idle for a moment.");
    } else if (!m_canvas->hasPicture()) {
      noticeTitle = tr("Waiting for the picture…");
    }
  } else if (m_state == QStringLiteral("denied")) {
    status = tr("Screen recording not allowed");
    noticeTitle = tr("%1 hasn't allowed screen recording").arg(m_client);
#ifdef Q_OS_MACOS
    noticeDetail = tr("On %1, open System Settings > Privacy & Security > Screen & System Audio Recording, "
                      "turn on Deskflow, then restart Deskflow there.")
                       .arg(m_client);
#else
    noticeDetail = tr("On %1, allow Deskflow to record the screen (on a Mac: System Settings > Privacy & Security > "
                      "Screen & System Audio Recording), then restart Deskflow there.")
                       .arg(m_client);
#endif
    interactive = false;
  } else if (m_state == QStringLiteral("disabled")) {
    status = tr("Viewing turned off");
    noticeTitle = tr("%1 doesn't allow remote viewing").arg(m_client);
    noticeDetail = tr("It can be turned on in Deskflow on %1, under Remote Control.").arg(m_client);
    interactive = false;
  } else if (m_state == QStringLiteral("unsupported")) {
    status = tr("Not available");
    noticeTitle = tr("%1 can't share its screen").arg(m_client);
    noticeDetail = m_detail == QStringLiteral("the client runs an older Deskflow")
                       ? tr("Update Deskflow on %1 to the same version as this computer.").arg(m_client)
                       : tr("Remote viewing works on macOS, Windows and Linux with X11; Wayland isn't supported yet.");
    interactive = false;
  } else if (m_state == QStringLiteral("failed")) {
    status = tr("Capture failed");
    noticeTitle = tr("Couldn't capture the screen of %1").arg(m_client);
    noticeDetail = m_detail;
    interactive = false;
  } else {
    state = QStringLiteral("busy");
    status = tr("Connecting…");
    noticeTitle = tr("Connecting to %1…").arg(m_client);
  }

  m_status->setText(status);
  m_dot->setProperty("state", state);
  m_dot->style()->unpolish(m_dot);
  m_dot->style()->polish(m_dot);
  m_canvas->setInteractive(interactive);
  m_canvas->setNotice(noticeTitle, noticeDetail);
}

void RemoteViewWindow::retranslate()
{
  setWindowTitle(tr("%1 — Remote Control").arg(m_client));
  m_quality->setSegmentText(0, tr("Smooth"));
  m_quality->setSegmentText(1, tr("Balanced"));
  m_quality->setSegmentText(2, tr("Sharp"));
  m_quality->setToolTip(tr("Smooth: smaller picture, less data. Sharp: full resolution, best for text."));
  m_fullScreen->setText(tr("Full Screen"));
  for (int i = 0; i < m_displays->count(); ++i)
    m_displays->setSegmentText(i, tr("Display %1").arg(i + 1));
  updateStatus();
}

void RemoteViewWindow::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::LanguageChange)
    retranslate();
  if (event->type() == QEvent::WindowStateChange)
    m_fullScreen->setText(isFullScreen() ? tr("Exit Full Screen") : tr("Full Screen"));
  QWidget::changeEvent(event);
}

void RemoteViewWindow::closeEvent(QCloseEvent *event)
{
  Q_EMIT command(deskflow::ipc::kRemoteInput, {{"client", m_client}, {"t", "leave"}});
  Q_EMIT command(deskflow::ipc::kRemoteView, {{"client", m_client}, {"on", false}});
  Q_EMIT closed(m_client);
  QWidget::closeEvent(event);
}

} // namespace deskflow::gui::remote
