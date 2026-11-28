/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "remoteview/StreamEncoder.h"

#include "remoteview/VideoCodec.h"

#include <QImageWriter>

#include <algorithm>
#include <cmath>

namespace deskflow::remote {

QSize presetMaxSize(Quality quality)
{
  switch (quality) {
  case Quality::Smooth:
    return {1600, 1000};
  case Quality::Sharp:
    return {3840, 2400};
  default:
    return {1920, 1200};
  }
}

namespace {

//! Bits per pixel per frame; a screen is mostly still, so the encoder rarely spends it all
double bitsPerPixel(Quality quality)
{
  switch (quality) {
  case Quality::Smooth:
    return 0.06;
  case Quality::Sharp:
    return 0.22;
  default:
    return 0.12;
  }
}

int jpegQuality(Quality quality)
{
  switch (quality) {
  case Quality::Smooth:
    return 60;
  case Quality::Sharp:
    return 92;
  default:
    return 80;
  }
}

} // namespace

QSize fitSize(const QSize &source, Quality quality)
{
  if (source.isEmpty())
    return {};
  QSize size = source.scaled(presetMaxSize(quality).boundedTo(source), Qt::KeepAspectRatio);
  size.setWidth(std::max(2, size.width() & ~1));
  size.setHeight(std::max(2, size.height() & ~1));
  return size;
}

StreamEncoder::StreamEncoder() = default;
StreamEncoder::~StreamEncoder() = default;

void StreamEncoder::configure(bool allowH264, Quality quality, int fps)
{
  if (allowH264 != m_allowH264 || quality != m_quality || fps != m_fps) {
    m_video.reset(); // reopened with the new settings on the next frame
    m_videoSize = QSize();
    m_wantKey = true;
  }
  m_allowH264 = allowH264;
  m_quality = quality;
  m_fps = std::clamp(fps, 1, 60);
  m_tiles.setQuality(jpegQuality(quality));
}

void StreamEncoder::requestKeyFrame()
{
  m_wantKey = true;
}

bool StreamEncoder::hasIdleWork() const
{
  return m_wantKey || (!m_video && m_tiles.hasPendingRefine());
}

QString StreamEncoder::codecName() const
{
  if (m_video)
    return QStringLiteral("H.264 · %1").arg(m_video->name());
  return QImageWriter::supportedImageFormats().contains("jpeg") ? QStringLiteral("JPEG") : QStringLiteral("PNG");
}

std::vector<Tile> StreamEncoder::encode(const QImage &image, bool &key)
{
  key = false;
  if (image.isNull())
    return {};

  if (m_allowH264 && !m_h264Failed) {
    if (!m_video || m_videoSize != image.size()) {
      m_video = createH264Encoder();
      const auto kbps = static_cast<int>(std::lround(
          std::clamp(image.width() * double(image.height()) * m_fps * bitsPerPixel(m_quality) / 1000, 2000.0, 60000.0)
      ));
      if (!m_video || !m_video->open(image.size(), m_fps, kbps)) {
        m_video.reset();
        m_h264Failed = true; // this computer has no usable H.264 encoder: tiles from now on
      } else {
        m_videoSize = image.size();
        m_wantKey = true;
      }
    }
    if (m_video) {
      const bool forceKey = m_wantKey;
      bool isKey = false;
      QByteArray unit = m_video->encode(image, forceKey, isKey);
      if (unit.isEmpty()) {
        // a codec that stops working mid-stream (driver reset, display change): carry on with tiles
        m_video.reset();
        m_h264Failed = true;
        m_tiles.reset();
        return encodeTiles(image, key);
      }
      m_wantKey = false;
      key = isKey || forceKey;
      Tile tile;
      tile.rect = QRect(QPoint(0, 0), image.size());
      tile.codec = Codec::H264;
      tile.data = std::move(unit);
      return {std::move(tile)};
    }
  }
  return encodeTiles(image, key);
}

std::vector<Tile> StreamEncoder::encodeTiles(const QImage &image, bool &key)
{
  if (m_wantKey) {
    m_tiles.reset();
    m_wantKey = false;
  }
  return m_tiles.encode(image, &key);
}

} // namespace deskflow::remote
