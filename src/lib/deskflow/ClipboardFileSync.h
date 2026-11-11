/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/FileTransfer.h"

#include <array>
#include <functional>
#include <optional>

namespace deskflow {

//! Makes copied files follow the clipboard to other computers
/*!
A platform clipboard holds local paths; another computer has no use for them.
On the way out this replaces the paths with an offer the FileTransferManager
can honour later. On the way in it applies the rest of the clipboard at once,
fetches the files behind the offer, and applies the clipboard again with the
fetched files once they are here.
*/
class ClipboardFileSync
{
public:
  //! Sets the local platform clipboard.
  using ApplyFunc = std::function<void(ClipboardID, const IClipboard *)>;

  /*!
  \c limit is the largest total size, in bytes, that is fetched automatically;
  0 turns copying files between computers off.
  */
  ClipboardFileSync(FileTransferManager &manager, quint64 limit, ApplyFunc apply);

  //! Prepare a clipboard that was just read from this computer for the wire.
  void outgoing(IClipboard *clipboard);

  //! Apply a clipboard that arrived from another computer.
  void incoming(ClipboardID id, const IClipboard *clipboard);

  //! A local app took the clipboard: what we were fetching no longer belongs on it.
  void localChanged(ClipboardID id);

  static QStringList splitPaths(const std::string &data);
  static std::string joinPaths(const QStringList &paths);

  //! \c paths as file:// URIs, joined (and optionally ended) by \c separator.
  static std::string toUriList(const QStringList &paths, const char *separator, bool terminate);
  //! The local paths in a URI list; comments and anything that isn't a local file are skipped.
  static QStringList fromUriList(const std::string &data);

private:
  struct Pending
  {
    FileTransferManager::Offer offer;
    Clipboard rest; //!< the clipboard without its files
  };

  void filesReady(const std::string &origin, uint32_t token, const QStringList &paths);

  FileTransferManager &m_manager;
  quint64 m_limit;
  ApplyFunc m_apply;
  std::array<std::optional<Pending>, kClipboardEnd> m_pending;
};

} // namespace deskflow
