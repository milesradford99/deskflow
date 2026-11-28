/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotThumbnail.h"

#include "ScreenshotEditor.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace deskflow::gui::screenshot {

namespace {
constexpr int kShadow = 18;       // room around the card for its shadow
constexpr int kScreenMargin = 20; // gap to the screen edges, as on macOS
constexpr QSize kMaxPreview(220, 150);
constexpr qreal kRadius = 10;
constexpr qreal kButton = 22;
} // namespace

ScreenshotThumbnail::ScreenshotThumbnail(QWidget *parent)
    : QWidget(
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint |
                      Qt::WindowDoesNotAcceptFocus
      ),
      m_timer(new QTimer(this)),
      m_slide(new QPropertyAnimation(this, "pos", this))
{
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_MacAlwaysShowToolWindow);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setToolTip(tr("Click to edit · Drag to drop into another app"));

  m_timer->setSingleShot(true);
  connect(m_timer, &QTimer::timeout, this, &ScreenshotThumbnail::dismiss);
  connect(m_slide, &QPropertyAnimation::finished, this, [this] {
    if (m_dismissing)
      hide();
  });
}

void ScreenshotThumbnail::present(const QImage &image, QScreen *screen, int seconds)
{
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  m_image = image;
  m_seconds = qMax(1, seconds);
  m_dismissing = false;
  m_hover = false;
  m_pressed = false;

  const qreal ratio = qMax(1.0, image.devicePixelRatio());
  QSizeF logical = QSizeF(image.size()) / ratio;
  logical = logical.scaled(QSizeF(kMaxPreview).boundedTo(logical.expandedTo(QSizeF(1, 1))), Qt::KeepAspectRatio);
  // Very thin strips still get a usable target.
  const QSize preview(qMax(72, qRound(logical.width())), qMax(48, qRound(logical.height())));

  const qreal screenRatio = screen->devicePixelRatio();
  m_preview = QPixmap::fromImage(
      image.scaled(QSizeF(preview * screenRatio).toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
  );
  m_preview.setDevicePixelRatio(screenRatio);

  setFixedSize(preview + QSize(2 * kShadow, 2 * kShadow));
  const QRect available = screen->availableGeometry();
  m_restPos = QPoint(
      available.right() + 1 - width() + kShadow - kScreenMargin,
      available.bottom() + 1 - height() + kShadow - kScreenMargin
  );

  m_slide->stop();
  m_slide->setDuration(320);
  m_slide->setEasingCurve(QEasingCurve::OutCubic);
  m_slide->setStartValue(QPoint(available.right() + 1, m_restPos.y()));
  m_slide->setEndValue(m_restPos);
  move(m_slide->startValue().toPoint());
  show();
  raise();
  m_slide->start();
  restartTimer(m_seconds * 1000);
  update();
}

void ScreenshotThumbnail::dismiss()
{
  if (!isVisible() || m_dismissing)
    return;
  m_timer->stop();
  m_dismissing = true;
  const auto *screen = this->screen();
  const int offscreen = (screen ? screen->geometry().right() : pos().x() + width()) + 1;
  m_slide->stop();
  m_slide->setDuration(240);
  m_slide->setEasingCurve(QEasingCurve::InCubic);
  m_slide->setStartValue(pos());
  m_slide->setEndValue(QPoint(offscreen, pos().y()));
  m_slide->start();
}

void ScreenshotThumbnail::hideNow()
{
  m_timer->stop();
  m_slide->stop();
  m_dismissing = false;
  hide();
}

void ScreenshotThumbnail::setHovered(bool hovered)
{
  m_hover = hovered;
  update();
}

void ScreenshotThumbnail::restartTimer(int msec)
{
  m_timer->start(msec);
}

QRectF ScreenshotThumbnail::imageRect() const
{
  return QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);
}

QRectF ScreenshotThumbnail::closeButtonRect() const
{
  const QRectF image = imageRect();
  return QRectF(image.left() + 7, image.top() + 7, kButton, kButton);
}

QRectF ScreenshotThumbnail::saveButtonRect() const
{
  const QRectF image = imageRect();
  return QRectF(image.right() - 7 - kButton, image.top() + 7, kButton, kButton);
}

void ScreenshotThumbnail::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QRectF image = imageRect();

  // Shadow: a few soft, slightly lowered layers.
  p.setPen(Qt::NoPen);
  for (int i = kShadow; i > 0; i -= 2) {
    const qreal alpha = 26.0 * (1.0 - qreal(i) / kShadow);
    p.setBrush(QColor(0, 0, 0, qRound(alpha)));
    p.drawRoundedRect(image.adjusted(-i * 0.6, -i * 0.4, i * 0.6, i * 0.8), kRadius + i * 0.5, kRadius + i * 0.5);
  }

  QPainterPath card;
  card.addRoundedRect(image, kRadius, kRadius);
  p.save();
  p.setClipPath(card);
  // Fill the card, cropping the preview to it like the macOS thumbnail does.
  const QSizeF pixmapSize = QSizeF(m_preview.size()) / m_preview.devicePixelRatio();
  const QRectF source = QRectF(QPointF(0, 0), pixmapSize);
  QRectF target = source;
  target.moveCenter(image.center());
  p.fillRect(image, QColor(0x30, 0x30, 0x34));
  p.drawPixmap(target, m_preview, QRectF(QPointF(0, 0), QSizeF(m_preview.size())));

  if (m_hover) {
    p.fillRect(image, QColor(0, 0, 0, 80));

    // "Edit" pill in the middle.
    QFont font = this->font();
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    const QString edit = tr("Edit");
    const QFontMetricsF metrics(font);
    QRectF pill(0, 0, metrics.horizontalAdvance(edit) + 40, 26);
    pill.moveCenter(image.center());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(pill, 13, 13);
    p.setPen(QPen(QColor(0x1C, 0x1C, 0x1E), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF pen = QPointF(pill.left() + 14, pill.center().y());
    p.drawLine(pen + QPointF(-3.5, 4), pen + QPointF(3.5, -3));
    p.drawLine(pen + QPointF(-3.5, 4), pen + QPointF(-4, 4.5));
    p.drawText(pill.adjusted(22, 0, -6, 0), Qt::AlignCenter, edit);

    // Round buttons: close (top left) and save (top right).
    for (const auto &button : {closeButtonRect(), saveButtonRect()}) {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(30, 30, 32, 210));
      p.drawEllipse(button);
    }
    p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF c = closeButtonRect().center();
    p.drawLine(c + QPointF(-3.5, -3.5), c + QPointF(3.5, 3.5));
    p.drawLine(c + QPointF(-3.5, 3.5), c + QPointF(3.5, -3.5));
    const QPointF s = saveButtonRect().center();
    p.drawLine(s + QPointF(0, -5), s + QPointF(0, 2.5));
    p.drawLine(s + QPointF(-3.5, -1), s + QPointF(0, 2.5));
    p.drawLine(s + QPointF(3.5, -1), s + QPointF(0, 2.5));
    p.drawLine(s + QPointF(-4.5, 5), s + QPointF(4.5, 5));
  }
  p.restore();

  // Hairline edge so light screenshots don't melt into light backgrounds.
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor(255, 255, 255, 90), 1));
  p.drawRoundedRect(image.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
  p.setPen(QPen(QColor(0, 0, 0, 50), 1));
  p.drawRoundedRect(image.adjusted(-0.5, -0.5, 0.5, 0.5), kRadius + 0.5, kRadius + 0.5);
}

void ScreenshotThumbnail::enterEvent(QEnterEvent *event)
{
  QWidget::enterEvent(event);
  m_hover = true;
  m_timer->stop();
  update();
}

void ScreenshotThumbnail::leaveEvent(QEvent *event)
{
  QWidget::leaveEvent(event);
  m_hover = false;
  if (!m_dismissing && isVisible())
    restartTimer(qMax(2500, m_seconds * 500));
  update();
}

void ScreenshotThumbnail::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton)
    return QWidget::mousePressEvent(event);
  m_pressed = true;
  m_pressPos = event->position().toPoint();
}

void ScreenshotThumbnail::mouseMoveEvent(QMouseEvent *event)
{
  if (m_pressed && (event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
    m_pressed = false;
    startDrag();
  }
}

void ScreenshotThumbnail::mouseReleaseEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton || !m_pressed)
    return QWidget::mouseReleaseEvent(event);
  m_pressed = false;
  const QPointF pos = event->position();
  if (closeButtonRect().contains(pos)) {
    dismiss();
  } else if (saveButtonRect().contains(pos)) {
    Q_EMIT saveRequested(m_image);
    dismiss();
  } else if (imageRect().contains(pos)) {
    hideNow();
    Q_EMIT editRequested(m_image);
  }
}

void ScreenshotThumbnail::startDrag()
{
  m_timer->stop();
  const auto dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const auto path = QDir(dir).filePath(ScreenshotEditor::defaultFileName());
  auto *mime = new QMimeData;
  if (m_image.save(path))
    mime->setUrls({QUrl::fromLocalFile(path)});
  mime->setImageData(m_image);

  auto *drag = new QDrag(this);
  drag->setMimeData(mime);
  QPixmap pixmap = m_preview.scaled(m_preview.size() * 0.6, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  pixmap.setDevicePixelRatio(m_preview.devicePixelRatio());
  drag->setPixmap(pixmap);
  drag->setHotSpot(
      QPoint(int(pixmap.width() / pixmap.devicePixelRatio() / 2), int(pixmap.height() / pixmap.devicePixelRatio() / 2))
  );
  const auto action = drag->exec(Qt::CopyAction);
  if (action != Qt::IgnoreAction)
    dismiss();
  else
    restartTimer(2500);
}

} // namespace deskflow::gui::screenshot
