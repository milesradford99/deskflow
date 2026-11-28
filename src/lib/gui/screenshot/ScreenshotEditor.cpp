/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotEditor.h"

#include "AnnotationCanvas.h"

#include "common/Settings.h"
#include "gui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace deskflow::gui::screenshot {

namespace {

struct ToolInfo
{
  Tool tool;
  Qt::Key key;
  const char *name;
};

// Single keys as in Shottr and CleanShot X.
const ToolInfo kTools[] = {
    {Tool::Select, Qt::Key_V, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Select and move")},
    {Tool::Crop, Qt::Key_C, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Crop")},
    {Tool::Rectangle, Qt::Key_R, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Rectangle")},
    {Tool::Ellipse, Qt::Key_O, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Ellipse")},
    {Tool::Arrow, Qt::Key_A, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Arrow")},
    {Tool::Line, Qt::Key_L, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Line")},
    {Tool::Pen, Qt::Key_P, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Pen")},
    {Tool::Highlight, Qt::Key_H, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Highlighter")},
    {Tool::Text, Qt::Key_T, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Text")},
    {Tool::Counter, Qt::Key_N, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Numbered step")},
    {Tool::Mosaic, Qt::Key_M, QT_TRANSLATE_NOOP("deskflow::gui::screenshot::ScreenshotEditor", "Pixelate")},
};

// macOS system colours.
const QColor kColors[] = {QColor(0xFF, 0x3B, 0x30), QColor(0xFF, 0xCC, 0x00), QColor(0x34, 0xC7, 0x59),
                          QColor(0x00, 0x7A, 0xFF), QColor(0xAF, 0x52, 0xDE), QColor(0x1C, 0x1C, 0x1E),
                          QColor(0xFF, 0xFF, 0xFF)};

//! Draw a tool symbol on a 24 x 24 grid with the painter's pen.
void drawToolSymbol(QPainter &p, Tool tool)
{
  const auto line = [&p](qreal x1, qreal y1, qreal x2, qreal y2) { p.drawLine(QPointF(x1, y1), QPointF(x2, y2)); };
  switch (tool) {
  case Tool::Select: {
    QPolygonF cursor;
    cursor << QPointF(7, 3.5) << QPointF(7, 18.5) << QPointF(10.8, 14.8) << QPointF(13.6, 20.6) << QPointF(16, 19.5)
           << QPointF(13.3, 13.8) << QPointF(18.5, 13.8);
    p.drawPolygon(cursor);
    break;
  }
  case Tool::Crop:
    line(7, 3, 7, 17);
    line(7, 17, 21, 17);
    line(3, 7, 17, 7);
    line(17, 7, 17, 21);
    break;
  case Tool::Rectangle:
    p.drawRoundedRect(QRectF(4, 6, 16, 12), 2, 2);
    break;
  case Tool::Ellipse:
    p.drawEllipse(QRectF(4, 5, 16, 14));
    break;
  case Tool::Arrow:
    line(5.5, 18.5, 18, 6);
    line(10.5, 6, 18, 6);
    line(18, 6, 18, 13.5);
    break;
  case Tool::Line:
    line(5, 19, 19, 5);
    break;
  case Tool::Pen: {
    QPolygonF pencil;
    pencil << QPointF(4.5, 19.5) << QPointF(5.5, 15.5) << QPointF(15.5, 5.5) << QPointF(18.5, 8.5)
           << QPointF(8.5, 18.5);
    p.drawPolygon(pencil);
    line(13.5, 7.5, 16.5, 10.5);
    break;
  }
  case Tool::Highlight: {
    p.save();
    QColor bar = p.pen().color();
    bar.setAlphaF(0.35);
    p.setPen(Qt::NoPen);
    p.setBrush(bar);
    p.drawRoundedRect(QRectF(3.5, 16, 17, 5), 1.5, 1.5);
    p.restore();
    QPolygonF marker;
    marker << QPointF(8, 14) << QPointF(14.5, 4) << QPointF(18.5, 6.5) << QPointF(12, 16.5);
    p.drawPolygon(marker);
    line(8, 14, 7, 16.5);
    line(7, 16.5, 9.5, 17);
    line(9.5, 17, 12, 16.5);
    break;
  }
  case Tool::Text:
    line(6, 5.5, 18, 5.5);
    line(12, 5.5, 12, 19);
    line(9.5, 19, 14.5, 19);
    break;
  case Tool::Counter: {
    p.drawEllipse(QPointF(12, 12), 8, 8);
    QFont font;
    font.setPixelSize(10);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRectF(4, 4, 16, 16), Qt::AlignCenter, QStringLiteral("1"));
    break;
  }
  case Tool::Mosaic: {
    p.save();
    p.setBrush(p.pen().color());
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        if ((row + col) % 2 == 0)
          p.drawRect(QRectF(4.5 + col * 5, 4.5 + row * 5, 5, 5));
      }
    }
    p.restore();
    p.drawRect(QRectF(4.5, 4.5, 15, 15));
    break;
  }
  }
}

QPixmap symbolPixmap(const std::function<void(QPainter &)> &draw, const QColor &color, qreal width = 1.6)
{
  constexpr qreal kRatio = 2;
  QPixmap pixmap(QSize(24, 24) * kRatio);
  pixmap.fill(Qt::transparent);
  QPainter p(&pixmap);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(kRatio, kRatio);
  p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  draw(p);
  p.end();
  pixmap.setDevicePixelRatio(kRatio);
  return pixmap;
}

QIcon twoToneIcon(const std::function<void(QPainter &)> &draw, const QColor &normal, const QColor &on)
{
  QIcon icon;
  icon.addPixmap(symbolPixmap(draw, normal), QIcon::Normal, QIcon::Off);
  icon.addPixmap(symbolPixmap(draw, on), QIcon::Normal, QIcon::On);
  QColor disabled = normal;
  disabled.setAlphaF(0.35);
  icon.addPixmap(symbolPixmap(draw, disabled), QIcon::Disabled, QIcon::Off);
  return icon;
}

void drawUndo(QPainter &p, bool mirrored)
{
  if (mirrored) {
    p.translate(24, 0);
    p.scale(-1, 1);
  }
  QPainterPath path;
  path.moveTo(18.5, 18);
  path.cubicTo(18.5, 12, 15, 9.5, 10, 9.5);
  path.lineTo(5.5, 9.5);
  p.drawPath(path);
  p.drawLine(QPointF(9, 6), QPointF(5.5, 9.5));
  p.drawLine(QPointF(5.5, 9.5), QPointF(9, 13));
}

//! A round colour well.
class ColorDot : public QAbstractButton
{
public:
  ColorDot(const QColor &color, QWidget *parent) : QAbstractButton(parent), m_color(color)
  {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(22, 26);
    setFocusPolicy(Qt::NoFocus);
  }
  QColor color() const
  {
    return m_color;
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF centre = QRectF(rect()).center();
    if (isChecked()) {
      p.setPen(QPen(theme::accent(), 2));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(centre, 9.5, 9.5);
    }
    p.setPen(QPen(theme::isDark() ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 40), 1));
    p.setBrush(m_color);
    p.drawEllipse(centre, isChecked() ? 6.5 : 7.5, isChecked() ? 6.5 : 7.5);
  }

private:
  QColor m_color;
};

QFrame *separator(QWidget *parent)
{
  auto *line = new QFrame(parent);
  line->setFixedSize(1, 20);
  line->setStyleSheet(QStringLiteral("background: %1;").arg(theme::border().name()));
  return line;
}

QString nativeShortcut(QKeySequence::StandardKey key)
{
  return QKeySequence(key).toString(QKeySequence::NativeText);
}

} // namespace

ScreenshotEditor::ScreenshotEditor(const QImage &image, QWidget *parent) : QWidget(parent, Qt::Window)
{
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Edit Screenshot"));
  buildUi();
  m_canvas->setImage(image);
  selectTool(Tool::Arrow);
  updateState();

  // Open at the screenshot's own size when it fits, on the screen with the cursor.
  const qreal ratio = qMax(1.0, image.devicePixelRatio());
  const QSize logical = (QSizeF(image.size()) / ratio).toSize();
  auto *screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  const QRect available = screen->availableGeometry();
  const QSize chrome(56, 56 + 52 + 30);
  const QSize size(
      qBound(qMin(900, available.width()), logical.width() + chrome.width(), int(available.width() * 0.88)),
      qBound(qMin(520, available.height()), logical.height() + chrome.height(), int(available.height() * 0.88))
  );
  resize(size);
  move(available.center() - QPoint(size.width() / 2, size.height() / 2));
}

void ScreenshotEditor::buildUi()
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto *bar = new QFrame(this);
  bar->setProperty("editorBar", true);
  auto *barLayout = new QHBoxLayout(bar);
  barLayout->setContentsMargins(12, 9, 12, 9);
  barLayout->setSpacing(10);

  // tools
  auto *toolTrack = new QFrame(bar);
  toolTrack->setProperty("segmentTrack", true);
  auto *toolLayout = new QHBoxLayout(toolTrack);
  toolLayout->setContentsMargins(2, 2, 2, 2);
  toolLayout->setSpacing(1);
  m_tools = new QButtonGroup(this);
  m_tools->setExclusive(true);
  for (const auto &info : kTools) {
    auto *button = new QToolButton(toolTrack);
    button->setProperty("editorTool", true);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(30, 28);
    button->setToolTip(
        QStringLiteral("%1  (%2)").arg(tr(info.name), QKeySequence(info.key).toString(QKeySequence::NativeText))
    );
    m_tools->addButton(button, int(info.tool));
    toolLayout->addWidget(button);
  }
  barLayout->addWidget(toolTrack);
  connect(m_tools, &QButtonGroup::idClicked, this, [this](int id) { selectTool(Tool(id)); });

  barLayout->addWidget(separator(bar));

  // colours
  auto *colorLayout = new QHBoxLayout;
  colorLayout->setSpacing(0);
  m_colors = new QButtonGroup(this);
  m_colors->setExclusive(true);
  const QStringList colorNames = {tr("Red"),    tr("Yellow"), tr("Green"), tr("Blue"),
                                  tr("Purple"), tr("Black"),  tr("White")};
  for (qsizetype i = 0; i < std::size(kColors); ++i) {
    auto *dot = new ColorDot(kColors[i], bar);
    dot->setToolTip(colorNames.value(i));
    dot->setChecked(i == 0);
    m_colors->addButton(dot, int(i));
    colorLayout->addWidget(dot);
  }
  barLayout->addLayout(colorLayout);
  connect(m_colors, &QButtonGroup::idClicked, this, [this](int id) {
    m_canvas->setColor(kColors[id]);
    m_canvas->setFocus();
  });

  barLayout->addWidget(separator(bar));

  // sizes
  auto *sizeTrack = new QFrame(bar);
  sizeTrack->setProperty("segmentTrack", true);
  auto *sizeLayout = new QHBoxLayout(sizeTrack);
  sizeLayout->setContentsMargins(2, 2, 2, 2);
  sizeLayout->setSpacing(1);
  m_sizes = new QButtonGroup(this);
  m_sizes->setExclusive(true);
  const QStringList sizeNames = {tr("Thin"), tr("Medium"), tr("Thick")};
  for (int i = 0; i < 3; ++i) {
    auto *button = new QToolButton(sizeTrack);
    button->setProperty("editorTool", true);
    button->setCheckable(true);
    button->setChecked(i == 1);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(28, 28);
    button->setToolTip(sizeNames.at(i));
    m_sizes->addButton(button, i);
    sizeLayout->addWidget(button);
  }
  barLayout->addWidget(sizeTrack);
  connect(m_sizes, &QButtonGroup::idClicked, this, [this](int id) {
    m_canvas->setStrokeLevel(id);
    m_canvas->setFocus();
  });

  barLayout->addStretch(1);

  m_undo = new QPushButton(bar);
  m_undo->setProperty("iconOnly", true);
  m_undo->setIconSize(QSize(18, 18));
  m_undo->setFocusPolicy(Qt::NoFocus);
  m_undo->setToolTip(tr("Undo (%1)").arg(nativeShortcut(QKeySequence::Undo)));
  barLayout->addWidget(m_undo);
  m_redo = new QPushButton(bar);
  m_redo->setProperty("iconOnly", true);
  m_redo->setIconSize(QSize(18, 18));
  m_redo->setFocusPolicy(Qt::NoFocus);
  m_redo->setToolTip(tr("Redo (%1)").arg(nativeShortcut(QKeySequence::Redo)));
  barLayout->addWidget(m_redo);

  m_save = new QPushButton(tr("Save…"), bar);
  m_save->setFocusPolicy(Qt::NoFocus);
  m_save->setToolTip(nativeShortcut(QKeySequence::Save));
  barLayout->addWidget(m_save);
  m_copy = new QPushButton(tr("Copy"), bar);
  m_copy->setProperty("primary", true);
  m_copy->setFocusPolicy(Qt::NoFocus);
  m_copy->setToolTip(tr("Copy to the clipboard and close (↵)"));
  barLayout->addWidget(m_copy);
  layout->addWidget(bar);

  m_canvas = new AnnotationCanvas(this);
  layout->addWidget(m_canvas, 1);

  auto *footer = new QHBoxLayout;
  footer->setContentsMargins(16, 6, 16, 8);
  footer->setSpacing(14);
  m_sizeLabel = new QLabel(this);
  m_sizeLabel->setProperty("tertiary", true);
  footer->addWidget(m_sizeLabel);
  auto *hints = new QLabel(this);
  hints->setProperty("tertiary", true);
  hints->setText(tr("↵ Copy    %1 Save    Esc Close").arg(nativeShortcut(QKeySequence::Save)));
  footer->addWidget(hints);
  footer->addStretch(1);
  m_status = new QLabel(this);
  m_status->setProperty("secondary", true);
  footer->addWidget(m_status);
  layout->addLayout(footer);

  connect(m_undo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::undo);
  connect(m_redo, &QPushButton::clicked, m_canvas, &AnnotationCanvas::redo);
  connect(m_save, &QPushButton::clicked, this, &ScreenshotEditor::save);
  connect(m_copy, &QPushButton::clicked, this, &ScreenshotEditor::copyAndClose);
  connect(m_canvas, &AnnotationCanvas::changed, this, [this] {
    m_finished = false;
    updateState();
  });
  connect(m_canvas, &AnnotationCanvas::cropFinished, this, [this] { selectTool(m_toolBeforeCrop); });

  const auto bind = [this](const QList<QKeySequence> &keys, auto slot) {
    auto *action = new QAction(this);
    action->setShortcuts(keys);
    action->setShortcutContext(Qt::WindowShortcut);
    connect(action, &QAction::triggered, this, slot);
    this->addAction(action);
  };
  bind(QKeySequence::keyBindings(QKeySequence::Undo), [this] { m_canvas->undo(); });
  bind(QKeySequence::keyBindings(QKeySequence::Redo) + QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Y)}, [this] {
    m_canvas->redo();
  });
  bind(QKeySequence::keyBindings(QKeySequence::Copy), [this] { copyAndClose(); });
  bind(QKeySequence::keyBindings(QKeySequence::Save), [this] { save(); });
  bind(QKeySequence::keyBindings(QKeySequence::Close), [this] { close(); });

  updateIcons();
}

void ScreenshotEditor::updateIcons()
{
  const QColor normal = theme::textSecondary();
  const QColor on = theme::accent();
  for (const auto &info : kTools) {
    const Tool tool = info.tool;
    m_tools->button(int(tool))->setIcon(twoToneIcon([tool](QPainter &p) { drawToolSymbol(p, tool); }, normal, on));
  }
  for (int i = 0; i < 3; ++i) {
    const qreal radius = 2.5 + i * 1.6;
    const auto draw = [radius](QPainter &p) {
      p.setBrush(p.pen().color());
      p.setPen(Qt::NoPen);
      p.drawEllipse(QPointF(12, 12), radius, radius);
    };
    m_sizes->button(i)->setIcon(twoToneIcon(draw, normal, on));
  }
  m_undo->setIcon(twoToneIcon([](QPainter &p) { drawUndo(p, false); }, normal, on));
  m_redo->setIcon(twoToneIcon([](QPainter &p) { drawUndo(p, true); }, normal, on));
}

void ScreenshotEditor::selectTool(Tool tool)
{
  if (tool == Tool::Crop && m_canvas->tool() != Tool::Crop)
    m_toolBeforeCrop = m_canvas->tool();
  m_canvas->setTool(tool);
  if (auto *button = m_tools->button(int(tool)))
    button->setChecked(true);
  m_canvas->setFocus();
  updateState();
}

void ScreenshotEditor::updateState()
{
  m_undo->setEnabled(m_canvas->canUndo());
  m_redo->setEnabled(m_canvas->canRedo());
  const QSize size = m_canvas->resultSize();
  m_sizeLabel->setText(tr("%1 × %2 px").arg(size.width()).arg(size.height()));
}

QString ScreenshotEditor::defaultFileName()
{
  return QStringLiteral("%1 %2.png")
      .arg(tr("Screenshot"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 'at' HH.mm.ss")));
}

void ScreenshotEditor::copyAndClose()
{
  m_canvas->commitText();
  Q_EMIT copyRequested(m_canvas->result());
  m_finished = true;
  close();
}

void ScreenshotEditor::save()
{
  m_canvas->commitText();
  auto folder = Settings::value(Settings::Screenshot::Folder).toString();
  if (folder.isEmpty() || !QDir(folder).exists())
    folder = QDir::homePath();
  const auto path = QFileDialog::getSaveFileName(
      this, tr("Save Screenshot"), QDir(folder).filePath(defaultFileName()),
      tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)")
  );
  if (path.isEmpty())
    return;
  if (!m_canvas->result().save(path)) {
    m_status->setText(tr("Couldn't save to %1").arg(QDir::toNativeSeparators(path)));
    return;
  }
  m_status->setText(tr("Saved to %1").arg(QDir::toNativeSeparators(path)));
  m_finished = true;
}

void ScreenshotEditor::keyPressEvent(QKeyEvent *event)
{
  const auto mods = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
  if (mods == Qt::NoModifier) {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      copyAndClose();
      return;
    case Qt::Key_Escape:
      close();
      return;
    default:
      for (const auto &info : kTools) {
        if (event->key() == info.key) {
          selectTool(info.tool);
          return;
        }
      }
      break;
    }
  }
  QWidget::keyPressEvent(event);
}

void ScreenshotEditor::closeEvent(QCloseEvent *event)
{
  m_canvas->commitText();
  if (!m_finished && m_canvas->isModified()) {
    const auto answer = QMessageBox::question(
        this, tr("Discard changes?"), tr("Close the editor without copying or saving your changes?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel
    );
    if (answer != QMessageBox::Discard) {
      event->ignore();
      return;
    }
  }
  event->accept();
}

void ScreenshotEditor::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange && m_tools)
    updateIcons();
}

} // namespace deskflow::gui::screenshot
