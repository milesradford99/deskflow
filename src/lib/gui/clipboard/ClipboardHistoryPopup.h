/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "ClipboardHistoryFilterModel.h"

#include <QWidget>

class QButtonGroup;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace deskflow::gui::clipboard {

class ClipboardHistoryModel;

/**
 * @brief The clipboard history panel: a keyboard first, Spotlight style window.
 *
 * Type to search, arrows to move, Enter to copy, Ctrl/Cmd+1-9 to pick one of
 * the first nine, Tab to switch category. The selected item is previewed on
 * the right with where and when it was copied.
 */
class ClipboardHistoryPopup : public QWidget
{
  Q_OBJECT
public:
  explicit ClipboardHistoryPopup(ClipboardHistoryModel *model, QWidget *parent = nullptr);

  /// Show on the screen under the mouse, focused and ready to type.
  void present();
  void toggle();

  void setSettingsMenu(QMenu *menu);
  void setSortMode(ClipboardHistoryFilterModel::SortMode mode);
  void setRecordingState(bool paused, bool ignoringNext);

  ClipboardHistoryFilterModel *filterModel() const
  {
    return m_filter;
  }

Q_SIGNALS:
  void copyRequested(const QString &id, bool plainTextOnly);
  void pinToggled(const QString &id);
  void removeRequested(const QString &id);
  void sortModeChanged(deskflow::gui::clipboard::ClipboardHistoryFilterModel::SortMode mode);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  void buildUi();
  void applyStyle();
  void updatePreview();
  void updateCounts();
  void selectRow(int row);
  void moveSelection(int delta);
  void setCategory(int category);
  void activate(bool plainTextOnly);
  void activateRow(int row, bool plainTextOnly);
  void togglePinCurrent();
  void removeCurrent();
  void showContextMenu(const QPoint &pos);
  void updateSortButton();
  QString currentId() const;

  ClipboardHistoryModel *m_model;
  ClipboardHistoryFilterModel *m_filter;

  QFrame *m_card = nullptr;
  QLineEdit *m_search = nullptr;
  QToolButton *m_sortButton = nullptr;
  QToolButton *m_moreButton = nullptr;
  QButtonGroup *m_categories = nullptr;
  QLabel *m_countLabel = nullptr;
  QStackedWidget *m_listStack = nullptr;
  QListView *m_list = nullptr;
  QLabel *m_emptyLabel = nullptr;

  QStackedWidget *m_previewStack = nullptr;
  QPlainTextEdit *m_previewText = nullptr;
  QLabel *m_previewImage = nullptr;
  QLabel *m_previewColor = nullptr;
  QPushButton *m_openLinkButton = nullptr;
  QLabel *m_metaLabel = nullptr;

  QLabel *m_hintsLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QMenu *m_sortMenu = nullptr;

  bool m_menuOpen = false;
  bool m_paused = false;
  bool m_ignoringNext = false;
};

} // namespace deskflow::gui::clipboard
