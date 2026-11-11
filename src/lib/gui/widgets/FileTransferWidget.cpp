/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferWidget.h"

#include "common/Settings.h"

#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace deskflow::gui {

namespace {

constexpr int kMaxFinishedRows = 4;

QStringList localFiles(const QMimeData *mime)
{
  QStringList paths;
  const auto urls = mime->urls();
  for (const auto &url : urls) {
    if (url.isLocalFile())
      paths.append(url.toLocalFile());
  }
  return paths;
}

void repolish(QWidget *widget)
{
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

} // namespace

FileTransferWidget::FileTransferWidget(QWidget *parent) : QFrame(parent)
{
  setObjectName("fileTransferCard");
  setProperty("card", true);
  setAcceptDrops(true);

  m_title = new QLabel(this);
  m_title->setProperty("heading", true);
  m_hint = new QLabel(this);
  m_hint->setProperty("secondary", true);
  m_hint->setWordWrap(true);

  m_peerArea = new QWidget(this);
  m_peerLayout = new QHBoxLayout(m_peerArea);
  m_peerLayout->setContentsMargins(0, 0, 0, 0);
  m_peerLayout->setSpacing(8);

  auto *rows = new QWidget(this);
  m_rowLayout = new QVBoxLayout(rows);
  m_rowLayout->setContentsMargins(0, 0, 0, 0);
  m_rowLayout->setSpacing(6);

  m_downloadLabel = new QLabel(this);
  m_downloadLabel->setProperty("secondary", true);
  m_downloadLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_downloadButton = new QPushButton(this);
  m_downloadButton->setFlat(true);
  m_downloadButton->setProperty("link", true);
  m_downloadButton->setCursor(Qt::PointingHandCursor);
  connect(m_downloadButton, &QPushButton::clicked, this, &FileTransferWidget::chooseDownloadFolder);

  auto *footer = new QHBoxLayout;
  footer->setSpacing(4);
  footer->addWidget(m_downloadLabel, 1);
  footer->addWidget(m_downloadButton);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 14, 16, 12);
  layout->setSpacing(8);
  layout->addWidget(m_title);
  layout->addWidget(m_hint);
  layout->addWidget(m_peerArea);
  layout->addWidget(rows);
  layout->addLayout(footer);

  // The core may run as another user (the Windows service), whose download
  // folder is not this user's. Pin the folder down while we know who is asking.
  if (Settings::value(Settings::Core::DownloadDir).toString().isEmpty()) {
    if (const auto dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation); !dir.isEmpty())
      Settings::setValue(Settings::Core::DownloadDir, dir);
  }

  retranslate();
  rebuildPeers();
}

QString FileTransferWidget::formatSize(qint64 bytes)
{
  return QLocale().formattedDataSize(bytes, bytes < 1024 * 1024 ? 0 : 1, QLocale::DataSizeTraditionalFormat);
}

void FileTransferWidget::retranslate()
{
  m_title->setText(tr("Send files"));
  m_downloadButton->setText(tr("Change…"));
  updateDownloadLabel();
  rebuildPeers();
}

void FileTransferWidget::updateDownloadLabel()
{
  const auto dir = Settings::value(Settings::Core::DownloadDir).toString();
  m_downloadLabel->setText(tr("Received files are saved to %1").arg(QDir::toNativeSeparators(dir)));
}

void FileTransferWidget::chooseDownloadFolder()
{
  const auto current = Settings::value(Settings::Core::DownloadDir).toString();
  const auto dir = QFileDialog::getExistingDirectory(this, tr("Save received files to"), current);
  if (dir.isEmpty())
    return;
  Settings::setValue(Settings::Core::DownloadDir, dir);
  updateDownloadLabel();
}

void FileTransferWidget::setPeers(const QStringList &peers)
{
  if (peers == m_peers)
    return;
  m_peers = peers;
  rebuildPeers();
}

void FileTransferWidget::reset()
{
  setPeers({});
  const auto ids = m_rowOrder;
  for (const auto id : ids) {
    if (m_rows.value(id).active)
      removeRow(id);
  }
}

void FileTransferWidget::rebuildPeers()
{
  qDeleteAll(m_peerButtons);
  m_peerButtons.clear();
  while (auto *item = m_peerLayout->takeAt(0))
    delete item;

  m_peerArea->setVisible(!m_peers.isEmpty());
  if (m_peers.isEmpty()) {
    m_hint->setText(tr("Connect another computer to send files to it. Files you copy are also pasted across."));
    return;
  }

  m_hint->setText(tr("Drop files or folders on a computer, or click it to choose them."));
  for (const auto &peer : std::as_const(m_peers)) {
    auto *button = new QToolButton(m_peerArea);
    button->setProperty("dropTarget", true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(QIcon::fromTheme(QStringLiteral("computer")));
    button->setText(peer);
    button->setToolTip(tr("Send files to %1").arg(peer));
    button->setCursor(Qt::PointingHandCursor);
    button->setPopupMode(QToolButton::MenuButtonPopup);

    auto *menu = new QMenu(button);
    connect(menu->addAction(tr("Choose files…")), &QAction::triggered, this, [this, peer] {
      chooseFiles(peer, false);
    });
    connect(menu->addAction(tr("Choose a folder…")), &QAction::triggered, this, [this, peer] {
      chooseFiles(peer, true);
    });
    button->setMenu(menu);
    connect(button, &QToolButton::clicked, this, [this, peer] { chooseFiles(peer, false); });

    m_peerLayout->addWidget(button);
    m_peerButtons.insert(peer, button);
  }
  m_peerLayout->addStretch(1);
}

void FileTransferWidget::chooseFiles(const QString &peer, bool folder)
{
  QStringList paths;
  if (folder) {
    if (const auto dir = QFileDialog::getExistingDirectory(this, tr("Send a folder to %1").arg(peer)); !dir.isEmpty())
      paths.append(dir);
  } else {
    paths = QFileDialog::getOpenFileNames(this, tr("Send files to %1").arg(peer));
  }
  send(peer, paths);
}

void FileTransferWidget::send(const QString &peer, const QStringList &paths)
{
  if (!peer.isEmpty() && !paths.isEmpty())
    Q_EMIT sendRequested(peer, paths);
}

QString FileTransferWidget::peerAt(const QPoint &pos) const
{
  for (auto it = m_peerButtons.cbegin(); it != m_peerButtons.cend(); ++it) {
    if (it.value()->geometry().translated(m_peerArea->pos()).contains(pos))
      return it.key();
  }
  // with a single computer there is nothing to aim at: the whole panel is the target
  return m_peers.size() == 1 ? m_peers.first() : QString();
}

void FileTransferWidget::setDropHighlight(const QString &peer, bool on)
{
  for (auto it = m_peerButtons.cbegin(); it != m_peerButtons.cend(); ++it) {
    const bool active = on && it.key() == peer;
    if (it.value()->property("dropActive").toBool() != active) {
      it.value()->setProperty("dropActive", active);
      repolish(it.value());
    }
  }
  if (property("dropActive").toBool() != on) {
    setProperty("dropActive", on);
    repolish(this);
  }
}

void FileTransferWidget::dragEnterEvent(QDragEnterEvent *event)
{
  if (m_peers.isEmpty() || localFiles(event->mimeData()).isEmpty())
    return;
  event->acceptProposedAction();
  setDropHighlight(peerAt(event->position().toPoint()), true);
}

void FileTransferWidget::dragMoveEvent(QDragMoveEvent *event)
{
  event->acceptProposedAction();
  setDropHighlight(peerAt(event->position().toPoint()), true);
}

void FileTransferWidget::dragLeaveEvent(QDragLeaveEvent *)
{
  setDropHighlight({}, false);
}

void FileTransferWidget::dropEvent(QDropEvent *event)
{
  setDropHighlight({}, false);
  const auto paths = localFiles(event->mimeData());
  if (paths.isEmpty() || m_peers.isEmpty())
    return;
  event->acceptProposedAction();

  auto peer = peerAt(event->position().toPoint());
  if (peer.isEmpty()) {
    // dropped between computers: ask which one was meant
    QMenu menu(this);
    for (const auto &name : std::as_const(m_peers))
      menu.addAction(tr("Send to %1").arg(name))->setData(name);
    if (const auto *chosen = menu.exec(mapToGlobal(event->position().toPoint())); chosen != nullptr)
      peer = chosen->data().toString();
  }
  send(peer, paths);
}

void FileTransferWidget::changeEvent(QEvent *event)
{
  QFrame::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    retranslate();
}

void FileTransferWidget::removeRow(quint32 id)
{
  if (const auto row = m_rows.take(id); row.frame != nullptr)
    row.frame->deleteLater();
  m_rowOrder.removeAll(id);
}

void FileTransferWidget::trimRows()
{
  // keep what is still moving, and only the most recent few results
  int finished = 0;
  for (auto it = m_rowOrder.crbegin(); it != m_rowOrder.crend(); ++it) {
    if (!m_rows.value(*it).active)
      ++finished;
  }
  const auto ids = m_rowOrder;
  for (const auto id : ids) {
    if (finished <= kMaxFinishedRows)
      break;
    if (!m_rows.value(id).active) {
      removeRow(id);
      --finished;
    }
  }
}

void FileTransferWidget::updateTransfer(const QJsonObject &progress)
{
  const auto id = static_cast<quint32>(progress["id"].toInteger());
  const auto state = progress["state"].toString();
  const bool incoming = progress["incoming"].toBool();
  const bool clipboard = progress["clipboard"].toBool();
  const auto peer = progress["peer"].toString();
  const auto total = progress["total"].toInteger();
  const auto done = progress["done"].toInteger();
  const auto detail = progress["detail"].toString();

  if (!m_rows.contains(id)) {
    Row row;
    row.frame = new QFrame(this);
    row.frame->setProperty("transferRow", true);
    row.title = new QLabel(row.frame);
    row.status = new QLabel(row.frame);
    row.status->setProperty("secondary", true);
    row.bar = new QProgressBar(row.frame);
    row.bar->setTextVisible(false);
    row.bar->setFixedHeight(4);
    row.action = new QToolButton(row.frame);
    row.action->setAutoRaise(true);
    row.action->setCursor(Qt::PointingHandCursor);
    connect(row.action, &QToolButton::clicked, this, [this, id] {
      const auto current = m_rows.value(id);
      if (current.active) {
        Q_EMIT cancelRequested(id);
      } else if (!current.location.isEmpty()) {
        // a single file is shown in its folder, several files are already a folder
        const QFileInfo info(current.location);
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.filePath() : info.absolutePath()));
      } else {
        removeRow(id);
      }
    });

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(row.title);
    text->addWidget(row.bar);
    text->addWidget(row.status);
    auto *layout = new QHBoxLayout(row.frame);
    layout->setContentsMargins(10, 8, 6, 8);
    layout->addLayout(text, 1);
    layout->addWidget(row.action);

    m_rowLayout->addWidget(row.frame);
    m_rows.insert(id, row);
    m_rowOrder.append(id);
  }

  auto &row = m_rows[id];
  const auto label = progress["label"].toString();
  if (clipboard) {
    row.title->setText(
        incoming ? tr("Copied files from %1: %2").arg(peer, label) : tr("Copied files to %1: %2").arg(peer, label)
    );
  } else {
    row.title->setText(incoming ? tr("%1 from %2").arg(label, peer) : tr("%1 to %2").arg(label, peer));
  }

  row.active = state == QLatin1String("active");
  row.bar->setVisible(row.active);
  row.bar->setRange(0, 1000);
  row.bar->setValue(total > 0 ? static_cast<int>(done * 1000 / total) : 0);

  if (row.active) {
    row.status->setText(tr("%1 of %2").arg(formatSize(done), formatSize(total)));
    row.action->setText(tr("Cancel"));
  } else if (state == QLatin1String("completed")) {
    row.location = incoming && !clipboard ? detail : QString();
    if (clipboard && incoming) {
      row.status->setText(tr("Ready to paste · %1").arg(formatSize(total)));
    } else {
      row.status->setText(
          incoming ? tr("Received · %1").arg(formatSize(total)) : tr("Sent · %1").arg(formatSize(total))
      );
    }
    row.action->setText(row.location.isEmpty() ? tr("Dismiss") : tr("Show in folder"));
  } else {
    row.status->setText(
        state == QLatin1String("cancelled") ? tr("Cancelled")
                                            : tr("Failed: %1").arg(detail.isEmpty() ? tr("unknown error") : detail)
    );
    row.status->setProperty("error", state != QLatin1String("cancelled"));
    repolish(row.status);
    row.action->setText(tr("Dismiss"));
  }

  trimRows();
}

} // namespace deskflow::gui
