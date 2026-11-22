/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RebuildablePage.h"

#include "gui/widgets/MacControls.h"

#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace deskflow::gui::pages {

RebuildablePage::RebuildablePage(QWidget *parent) : QWidget(parent)
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
}

void RebuildablePage::rebuild()
{
  m_rebuildPending = false;

  // keep the scroll position across a rebuild
  int scroll = 0;
  if (m_page) {
    if (auto *area = m_page->findChild<QScrollArea *>())
      scroll = area->verticalScrollBar()->value();
    m_page->hide();
    m_page->deleteLater();
  }

  m_page = new mac::Page(this);
  build(m_page);
  layout()->addWidget(m_page);

  if (scroll > 0) {
    QTimer::singleShot(0, m_page, [page = m_page, scroll] {
      if (auto *area = page->findChild<QScrollArea *>())
        area->verticalScrollBar()->setValue(scroll);
    });
  }
}

void RebuildablePage::scheduleRebuild()
{
  if (m_rebuildPending)
    return;
  m_rebuildPending = true;
  QTimer::singleShot(0, this, &RebuildablePage::rebuild);
}

void RebuildablePage::showEvent(QShowEvent *event)
{
  QWidget::showEvent(event);
  if (!event->spontaneous())
    rebuild();
}

void RebuildablePage::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange && m_page)
    scheduleRebuild();
}

} // namespace deskflow::gui::pages
