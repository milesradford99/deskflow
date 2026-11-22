/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogView.h"
#include "LogWidget.h"
#include "SearchWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

LogView::LogView(QWidget *parent)
    : QWidget(parent),
      m_textLog{new LogWidget(this)},
      m_lblTitle{new QLabel(this)},
      m_lblHint{new QLabel(this)},
      m_searchWidget{new SearchWidget(this)}
{
  connect(m_searchWidget, &SearchWidget::findNext, m_textLog, &LogWidget::findNext);
  connect(m_searchWidget, &SearchWidget::findPrevious, m_textLog, &LogWidget::findPrevious);

  m_lblTitle->setProperty("title", true);
  m_lblHint->setProperty("secondary", true);
  m_lblHint->setWordWrap(true);
  if (auto *text = m_textLog->findChild<QPlainTextEdit *>())
    text->setProperty("log", true);

  auto *header = new QHBoxLayout();
  header->addWidget(m_lblTitle);
  header->addStretch(1);
  header->addWidget(m_searchWidget, 0, Qt::AlignVCenter);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(32, 28, 32, 20);
  layout->setSpacing(6);
  layout->addLayout(header);
  layout->addWidget(m_lblHint);
  layout->addSpacing(10);
  layout->addWidget(m_textLog, 1);

  updateText();
}

void LogView::appendLine(const QString &msg)
{
  m_textLog->appendLine(msg);
}

void LogView::updateText()
{
  m_lblTitle->setText(tr("Log"));
  m_lblHint->setText(tr("What Deskflow is doing right now. Useful when something doesn't connect."));
}

void LogView::changeEvent(QEvent *e)
{
  QWidget::changeEvent(e);
  if (e->type() == QEvent::LanguageChange)
    updateText();
}
