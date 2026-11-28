/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Annotation.h"

#include <QPixmap>
#include <QPointer>
#include <QWidget>

class QLineEdit;

namespace deskflow::gui::screenshot {

/**
 * @brief Shows a screenshot fitted to the widget and lets the user crop it and draw on it.
 *
 * Everything is kept in image pixels and only flattened by result(), so marks
 * stay editable and every step can be undone.
 */
class AnnotationCanvas : public QWidget
{
  Q_OBJECT
public:
  explicit AnnotationCanvas(QWidget *parent = nullptr);

  //! Start over with @p image; its device pixel ratio decides the size it is shown at.
  void setImage(const QImage &image);
  //! The screenshot cropped and with every mark drawn in, in full resolution.
  QImage result() const;
  //! Size of result() in pixels.
  QSize resultSize() const;

  void setTool(Tool tool);
  Tool tool() const
  {
    return m_tool;
  }
  void setColor(const QColor &color);
  QColor color() const
  {
    return m_color;
  }
  //! Stroke size: 0 thin, 1 medium, 2 thick.
  void setStrokeLevel(int level);
  int strokeLevel() const
  {
    return m_level;
  }

  bool canUndo() const
  {
    return !m_undo.isEmpty();
  }
  bool canRedo() const
  {
    return !m_redo.isEmpty();
  }
  bool isModified() const
  {
    return canUndo();
  }

  void undo();
  void redo();
  //! Remove the selected mark, if any.
  bool deleteSelection();
  //! Finish typing, if a text box is open.
  void commitText();
  //! Go back to the whole screenshot.
  void resetCrop();

  //! For previews and tests: add a finished mark.
  void addAnnotation(const Annotation &annotation);

Q_SIGNALS:
  //! Marks, crop or undo history changed.
  void changed();
  //! Enter or a double click while cropping: the user is happy with the crop.
  void cropFinished();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct State
  {
    QList<Annotation> annotations;
    QRectF crop;
  };
  enum class Drag
  {
    None,
    Draw,
    Move,
    CropNew,
    CropMove,
    CropResize
  };

  State state() const;
  void pushUndo();
  void restore(const State &state);

  //! The part of the image the view frames: the crop, or everything while cropping.
  QRectF framedRect() const;
  QTransform imageToView() const;
  QPointF toImage(const QPointF &viewPos) const;
  qreal viewScale() const;
  qreal sizeForTool(Tool tool) const;
  int hitAnnotation(const QPointF &imagePos) const;
  //! Crop handle under @p viewPos: 0-7 clockwise from top left, or -1.
  int cropHandleAt(const QPointF &viewPos) const;
  QList<QPointF> cropHandles() const;
  void updateCursor(const QPointF &viewPos);
  void beginText(const QPointF &imagePos, const QString &text = QString());
  void positionTextEditor();
  int nextCounter() const;

  QImage m_image;
  QPixmap m_pixmap;
  qreal m_dpr = 1;

  QList<Annotation> m_annotations;
  QRectF m_crop;
  QList<State> m_undo;
  QList<State> m_redo;

  Tool m_tool = Tool::Arrow;
  QColor m_color;
  int m_level = 1;

  Drag m_drag = Drag::None;
  QPointF m_pressImage;
  QPointF m_lastImage;
  QRectF m_pressCrop;
  int m_cropHandle = -1;
  Annotation m_current;
  int m_selected = -1;

  QPointer<QLineEdit> m_textEditor;
  QPointF m_textAnchor;
  QColor m_textColor;
  qreal m_textSize = 0;
  bool m_textUndoPushed = false;
};

} // namespace deskflow::gui::screenshot
