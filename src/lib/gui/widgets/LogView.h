/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class LogWidget;
class QLabel;
class SearchWidget;

//! The log as a page of the main window, with search.
class LogView : public QWidget
{
  Q_OBJECT
public:
  explicit LogView(QWidget *parent = nullptr);
  void appendLine(const QString &msg);

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  LogWidget *m_textLog = nullptr;
  QLabel *m_lblTitle = nullptr;
  QLabel *m_lblHint = nullptr;
  SearchWidget *m_searchWidget = nullptr;
};
