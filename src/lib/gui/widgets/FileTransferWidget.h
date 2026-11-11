/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QFrame>
#include <QHash>
#include <QJsonObject>
#include <QStringList>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace deskflow::gui {

//! Send files to other computers and watch transfers in both directions
/*!
Files are dropped on the panel (or on one computer in it) or picked with a
file dialog. The widget only talks in signals and plain data; the core does
the transferring and reports progress back through updateTransfer().
*/
class FileTransferWidget : public QFrame
{
  Q_OBJECT

public:
  explicit FileTransferWidget(QWidget *parent = nullptr);

  //! The computers files can be sent to right now.
  void setPeers(const QStringList &peers);
  //! Progress of one transfer, as broadcast by the core (see FileTransferIpc).
  void updateTransfer(const QJsonObject &progress);
  //! The core went away: nothing is connected and nothing is moving any more.
  void reset();

  static QString formatSize(qint64 bytes);

Q_SIGNALS:
  void sendRequested(const QString &target, const QStringList &paths);
  void cancelRequested(quint32 id);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  struct Row
  {
    QFrame *frame = nullptr;
    QLabel *title = nullptr;
    QLabel *status = nullptr;
    QProgressBar *bar = nullptr;
    QToolButton *action = nullptr;
    QString location;
    bool active = true;
  };

  void retranslate();
  void rebuildPeers();
  void chooseFiles(const QString &peer, bool folder);
  void send(const QString &peer, const QStringList &paths);
  QString peerAt(const QPoint &pos) const;
  void setDropHighlight(const QString &peer, bool on);
  void chooseDownloadFolder();
  void updateDownloadLabel();
  void removeRow(quint32 id);
  void trimRows();

  QStringList m_peers;
  QLabel *m_title = nullptr;
  QLabel *m_hint = nullptr;
  QWidget *m_peerArea = nullptr;
  QHBoxLayout *m_peerLayout = nullptr;
  QVBoxLayout *m_rowLayout = nullptr;
  QLabel *m_downloadLabel = nullptr;
  QPushButton *m_downloadButton = nullptr;
  QHash<QString, QToolButton *> m_peerButtons;
  QHash<quint32, Row> m_rows;
  QList<quint32> m_rowOrder;
};

} // namespace deskflow::gui
