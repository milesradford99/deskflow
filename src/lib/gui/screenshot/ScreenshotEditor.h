/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "Annotation.h"

#include <QHash>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QLabel;
class QPushButton;

namespace deskflow::gui::screenshot {

class AnnotationCanvas;

/**
 * @brief A window to crop and mark up a screenshot, then copy or save it.
 *
 * Tools after CleanShot X, Shottr and Snipaste: crop, rectangle, ellipse,
 * arrow, line, pen, highlighter, text, numbered steps and pixelate, in a few
 * colours and three sizes. Single keys pick tools; Enter copies and closes.
 */
class ScreenshotEditor : public QWidget
{
  Q_OBJECT
public:
  explicit ScreenshotEditor(const QImage &image, QWidget *parent = nullptr);

  AnnotationCanvas *canvas() const
  {
    return m_canvas;
  }
  void selectTool(Tool tool);

  //! Name for a new screenshot file, like macOS: "Screenshot 2026-11-24 at 12.00.00.png".
  static QString defaultFileName();

Q_SIGNALS:
  //! The user finished: put @p image on the clipboard.
  void copyRequested(const QImage &image);

protected:
  void keyPressEvent(QKeyEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void buildUi();
  void updateIcons();
  void updateState();
  void copyAndClose();
  void save();

  AnnotationCanvas *m_canvas = nullptr;
  QButtonGroup *m_tools = nullptr;
  QButtonGroup *m_colors = nullptr;
  QButtonGroup *m_sizes = nullptr;
  QPushButton *m_undo = nullptr;
  QPushButton *m_redo = nullptr;
  QPushButton *m_save = nullptr;
  QPushButton *m_copy = nullptr;
  QLabel *m_sizeLabel = nullptr;
  QLabel *m_status = nullptr;
  Tool m_toolBeforeCrop = Tool::Arrow;
  bool m_finished = false;
};

} // namespace deskflow::gui::screenshot
