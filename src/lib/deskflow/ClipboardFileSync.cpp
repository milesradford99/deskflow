/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardFileSync.h"

#include "base/Log.h"

#include <QByteArrayList>
#include <QUrl>

namespace deskflow {

namespace {

using Format = IClipboard::Format;

//! Copy \c source into \c target, keeping only the formats \c keep accepts.
template <typename Keep> void copyFormats(Clipboard &target, const IClipboard *source, IClipboard::Time time, Keep keep)
{
  if (!target.open(time))
    return;
  target.empty();
  if (source->open(time)) {
    for (int i = 0; i < static_cast<int>(Format::TotalFormats); ++i) {
      const auto format = static_cast<Format>(i);
      if (keep(format) && source->has(format))
        target.add(format, source->get(format));
    }
    source->close();
  }
  target.close();
}

} // namespace

ClipboardFileSync::ClipboardFileSync(FileTransferManager &manager, quint64 limit, ApplyFunc apply)
    : m_manager(manager),
      m_limit(limit),
      m_apply(std::move(apply))
{
  m_manager.setClipboardFilesHandler([this](const std::string &origin, uint32_t token, const QStringList &paths) {
    filesReady(origin, token, paths);
  });
}

QStringList ClipboardFileSync::splitPaths(const std::string &data)
{
  return QString::fromStdString(data).split('\n', Qt::SkipEmptyParts);
}

std::string ClipboardFileSync::joinPaths(const QStringList &paths)
{
  return paths.join('\n').toStdString();
}

std::string ClipboardFileSync::toUriList(const QStringList &paths, const char *separator, bool terminate)
{
  QByteArrayList uris;
  for (const auto &path : paths)
    uris.append(QUrl::fromLocalFile(path).toEncoded());

  auto list = uris.join(separator);
  if (terminate && !list.isEmpty())
    list.append(separator);
  return list.toStdString();
}

QStringList ClipboardFileSync::fromUriList(const std::string &data)
{
  QStringList paths;
  const auto lines = QString::fromStdString(data).split('\n', Qt::SkipEmptyParts);
  for (const auto &rawLine : lines) {
    const auto line = rawLine.trimmed();
    if (line.isEmpty() || line.startsWith('#'))
      continue;
    if (const QUrl url(line); url.isLocalFile())
      paths.append(url.toLocalFile());
  }
  return paths;
}

void ClipboardFileSync::outgoing(IClipboard *clipboard)
{
  const auto time = clipboard->getTime();
  if (!clipboard->open(time))
    return;

  if (!clipboard->has(Format::Files)) {
    clipboard->close();
    return;
  }

  const auto paths = splitPaths(clipboard->get(Format::Files));
  const auto text = clipboard->has(Format::Text) ? clipboard->get(Format::Text) : std::string();
  const auto offer = m_limit > 0 ? m_manager.offerForLocalPaths(paths) : std::string();

  // File managers also put the file's icon and other previews on the clipboard.
  // Next to real files they would only be pasted by mistake, so send names and files alone.
  clipboard->empty();
  if (!text.empty())
    clipboard->add(Format::Text, text);
  if (!offer.empty())
    clipboard->add(Format::Files, offer);
  clipboard->close();
}

void ClipboardFileSync::incoming(ClipboardID id, const IClipboard *clipboard)
{
  if (id >= kClipboardEnd)
    return;
  m_pending[id].reset();

  const auto time = clipboard->getTime();
  std::string offerJson;
  if (clipboard->open(time)) {
    if (clipboard->has(Format::Files))
      offerJson = clipboard->get(Format::Files);
    clipboard->close();
  }

  if (offerJson.empty()) {
    m_apply(id, clipboard);
    return;
  }

  Clipboard rest;
  copyFormats(rest, clipboard, time, [](Format format) { return format != Format::Files; });

  const auto offer = FileTransferManager::parseOffer(offerJson);
  if (const auto paths = m_manager.localPathsForOffer(offer); !paths.isEmpty()) {
    // our own files coming back, or files we already fetched
    if (rest.open(time)) {
      rest.add(Format::Files, joinPaths(paths));
      rest.close();
    }
    m_apply(id, &rest);
    return;
  }

  // until the files are here, paste what we do have (usually the file names)
  m_apply(id, &rest);

  if (!offer.valid || m_limit == 0) {
    return;
  }

  if (offer.total > m_limit) {
    LOG_INFO(
        "copied files from \"%s\" are larger than the automatic limit (%llu MB), send them from the app instead",
        offer.origin.c_str(), static_cast<unsigned long long>(m_limit / (1024 * 1024))
    );
    return;
  }

  if (m_manager.requestOffer(offer)) {
    LOG_DEBUG("fetching %d copied file(s) from \"%s\"", offer.count, offer.origin.c_str());
    m_pending[id].emplace();
    m_pending[id]->offer = offer;
    copyFormats(m_pending[id]->rest, &rest, time, [](Format) { return true; });
  }
}

void ClipboardFileSync::localChanged(ClipboardID id)
{
  if (id < kClipboardEnd)
    m_pending[id].reset();
}

void ClipboardFileSync::filesReady(const std::string &origin, uint32_t token, const QStringList &paths)
{
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    auto &pending = m_pending[id];
    if (!pending || pending->offer.origin != origin || pending->offer.token != token)
      continue;

    LOG_INFO("copied files from \"%s\" are ready to paste", origin.c_str());
    const auto time = pending->rest.getTime();
    if (pending->rest.open(time)) {
      pending->rest.add(Format::Files, joinPaths(paths));
      pending->rest.close();
    }
    Clipboard ready;
    copyFormats(ready, &pending->rest, time, [](Format) { return true; });
    pending.reset();
    m_apply(id, &ready);
  }
}

} // namespace deskflow
