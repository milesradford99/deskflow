/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "remoteview/RemoteFrame.h"

#include "remoteview/VideoCodec.h"

#include <QBuffer>
#include <QDataStream>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

namespace deskflow::remote {

namespace {

constexpr uint8_t kVersion = 1;
constexpr int kMaxRunTiles = 8;   //!< widest rectangle, in tiles
constexpr int kMaxStackTiles = 4; //!< tallest rectangle, in tiles
constexpr int kSettledFrames = 2; //!< frames a tile must stay unchanged before it is sharpened
constexpr int kRefineBudget = 48; //!< tiles sharpened per frame
constexpr int kMaxTiles = 0xFFFF; //!< tiles in one piece
constexpr uint16_t kMaxSide = 16384;
constexpr uint8_t kMoreFlag = 0x80; //!< on the codec byte: the tile's data continues in the next tile
constexpr qsizetype kMaxFrameBytes = 32 * 1024 * 1024;

bool jpegAvailable()
{
  static const bool available = QImageWriter::supportedImageFormats().contains("jpeg");
  return available;
}

//! Rectangles covering the marked tiles: runs along each row, stacked when the row above has the same run
std::vector<QRect> rectsFromMask(const std::vector<uint8_t> &mask, int columns, int rows, const QSize &size)
{
  struct Open
  {
    int top;
    int height;
  };
  std::vector<QRect> rects;
  std::map<std::pair<int, int>, Open> open; // (first column, last column) -> run started above

  const auto close = [&](const std::pair<int, int> &run, const Open &o) {
    const int tile = FrameEncoder::kTileSize;
    QRect rect(run.first * tile, o.top * tile, (run.second - run.first + 1) * tile, o.height * tile);
    rects.push_back(rect.intersected(QRect(QPoint(0, 0), size)));
  };

  for (int row = 0; row < rows; ++row) {
    std::map<std::pair<int, int>, Open> next;
    int column = 0;
    while (column < columns) {
      if (!mask[row * columns + column]) {
        ++column;
        continue;
      }
      const int first = column;
      while (column < columns && mask[row * columns + column] && column - first < kMaxRunTiles)
        ++column;
      const std::pair run{first, column - 1};
      if (auto it = open.find(run); it != open.end() && it->second.height < kMaxStackTiles) {
        next[run] = {it->second.top, it->second.height + 1};
        open.erase(it);
      } else {
        next[run] = {row, 1};
      }
    }
    for (const auto &[run, o] : open)
      close(run, o);
    open = std::move(next);
  }
  for (const auto &[run, o] : open)
    close(run, o);
  return rects;
}

} // namespace

QByteArray writePiece(const FrameHeader &header, const std::vector<Tile> &tiles)
{
  QByteArray data;
  QDataStream out(&data, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::BigEndian);
  out << kVersion << header.flags << header.sequence;
  out << qint32(header.source.x()) << qint32(header.source.y()) << qint32(header.source.width())
      << qint32(header.source.height());
  out << quint16(header.size.width()) << quint16(header.size.height());
  out << header.display << header.displayCount;
  out << quint16(std::min<size_t>(tiles.size(), kMaxTiles));
  for (size_t i = 0; i < tiles.size() && i < kMaxTiles; ++i) {
    const auto &tile = tiles[i];
    out << quint16(tile.rect.x()) << quint16(tile.rect.y()) << quint16(tile.rect.width())
        << quint16(tile.rect.height());
    out << uint8_t(static_cast<uint8_t>(tile.codec) | (tile.more ? kMoreFlag : 0)) << quint32(tile.data.size());
    out.writeRawData(tile.data.constData(), static_cast<int>(tile.data.size()));
  }
  return data;
}

namespace {

bool readHeaderFrom(QDataStream &in, FrameHeader &header, quint16 &count)
{
  uint8_t version = 0;
  in >> version;
  if (version != kVersion)
    return false;

  qint32 sx = 0;
  qint32 sy = 0;
  qint32 sw = 0;
  qint32 sh = 0;
  quint16 width = 0;
  quint16 height = 0;
  in >> header.flags >> header.sequence >> sx >> sy >> sw >> sh >> width >> height;
  in >> header.display >> header.displayCount >> count;
  if (in.status() != QDataStream::Ok || width == 0 || height == 0 || width > kMaxSide || height > kMaxSide)
    return false;
  header.source = QRect(sx, sy, sw, sh);
  header.size = QSize(width, height);
  return true;
}

} // namespace

bool readHeader(const QByteArray &data, FrameHeader &header)
{
  QDataStream in(data);
  in.setByteOrder(QDataStream::BigEndian);
  quint16 count = 0;
  return readHeaderFrom(in, header, count);
}

bool readPiece(const QByteArray &data, FrameHeader &header, std::vector<Tile> &tiles)
{
  QDataStream in(data);
  in.setByteOrder(QDataStream::BigEndian);
  quint16 count = 0;
  if (!readHeaderFrom(in, header, count))
    return false;

  const QRect bounds(QPoint(0, 0), header.size);
  tiles.clear();
  tiles.reserve(count);
  for (int i = 0; i < count; ++i) {
    quint16 x = 0;
    quint16 y = 0;
    quint16 w = 0;
    quint16 h = 0;
    uint8_t codec = 0;
    quint32 size = 0;
    in >> x >> y >> w >> h >> codec >> size;
    const uint8_t kind = codec & ~kMoreFlag;
    if (in.status() != QDataStream::Ok || kind > static_cast<uint8_t>(Codec::H264) ||
        size > static_cast<quint32>(data.size()))
      return false;
    Tile tile;
    tile.rect = QRect(x, y, w, h);
    if (tile.rect.isEmpty() || !bounds.contains(tile.rect))
      return false;
    tile.codec = static_cast<Codec>(kind);
    tile.more = (codec & kMoreFlag) != 0;
    tile.data.resize(size);
    if (in.readRawData(tile.data.data(), static_cast<int>(size)) != static_cast<int>(size))
      return false;
    tiles.push_back(std::move(tile));
  }
  return in.status() == QDataStream::Ok;
}

std::vector<QByteArray> splitIntoPieces(FrameHeader header, std::vector<Tile> tiles)
{
  std::vector<QByteArray> pieces;
  std::vector<Tile> batch;
  qsizetype batchBytes = 0;
  const uint8_t key = header.flags & FrameHeader::kKeyFrame;

  const auto flush = [&](bool last) {
    header.flags = key | (last ? FrameHeader::kLastPiece : 0);
    pieces.push_back(writePiece(header, batch));
    batch.clear();
    batchBytes = 0;
  };

  const auto add = [&](Tile tile) {
    const auto bytes = tile.data.size() + 16;
    if (!batch.empty() && (batchBytes + bytes > kMaxPieceBytes || batch.size() >= kMaxTiles))
      flush(false);
    batchBytes += bytes;
    batch.push_back(std::move(tile));
  };

  for (auto &tile : tiles) {
    if (tile.data.size() <= kMaxPieceBytes) {
      add(std::move(tile));
      continue;
    }
    // a big tile (an H.264 key frame) goes in slices, one per piece
    for (qsizetype offset = 0; offset < tile.data.size(); offset += kMaxPieceBytes) {
      Tile slice;
      slice.rect = tile.rect;
      slice.codec = tile.codec;
      slice.data = tile.data.mid(offset, kMaxPieceBytes);
      slice.more = offset + kMaxPieceBytes < tile.data.size();
      add(std::move(slice));
    }
  }
  flush(true);
  return pieces;
}

bool FrameAssembler::add(const QByteArray &piece)
{
  FrameHeader header;
  std::vector<Tile> tiles;
  if (!readPiece(piece, header, tiles)) {
    m_open = false;
    m_tiles.clear();
    return false;
  }
  if (!m_open || header.sequence != m_header.sequence) {
    m_tiles.clear(); // a new frame, or the rest of the previous one never came
    m_open = true;
  }
  m_header = header;

  qsizetype total = 0;
  for (const auto &tile : m_tiles)
    total += tile.data.size();
  for (auto &tile : tiles) {
    total += tile.data.size();
    if (total > kMaxFrameBytes) {
      m_open = false;
      m_tiles.clear();
      return false;
    }
    if (!m_tiles.empty() && m_tiles.back().more) {
      auto &previous = m_tiles.back();
      if (previous.rect != tile.rect || previous.codec != tile.codec) {
        m_open = false;
        m_tiles.clear();
        return false;
      }
      previous.data += tile.data;
      previous.more = tile.more;
    } else {
      m_tiles.push_back(std::move(tile));
    }
  }

  if (!(header.flags & FrameHeader::kLastPiece))
    return false;
  m_open = false;
  if (!m_tiles.empty() && m_tiles.back().more) {
    m_tiles.clear();
    return false;
  }
  return true;
}

void FrameEncoder::reset()
{
  m_reset = true;
}

bool FrameEncoder::hasPendingRefine() const
{
  return std::any_of(m_lossy.begin(), m_lossy.end(), [](uint8_t lossy) { return lossy != 0; });
}

std::vector<QRect> FrameEncoder::changedRects(const QImage &image)
{
  const int tile = kTileSize;
  std::vector<uint8_t> mask(static_cast<size_t>(m_columns) * m_rows, 0);
  const int bytesPerPixel = image.depth() / 8;
  for (int row = 0; row < m_rows; ++row) {
    const int top = row * tile;
    const int bottom = std::min(top + tile, image.height());
    for (int column = 0; column < m_columns; ++column) {
      const int left = column * tile;
      const int width = std::min(tile, image.width() - left);
      const auto bytes = static_cast<size_t>(width) * bytesPerPixel;
      bool changed = false;
      for (int y = top; y < bottom && !changed; ++y) {
        changed =
            std::memcmp(
                image.constScanLine(y) + left * bytesPerPixel, m_previous.constScanLine(y) + left * bytesPerPixel, bytes
            ) != 0;
      }
      const auto index = static_cast<size_t>(row) * m_columns + column;
      if (changed) {
        mask[index] = 1;
        m_age[index] = 0;
      } else if (m_age[index] < 255) {
        ++m_age[index];
      }
    }
  }
  std::vector<QRect> rects = rectsFromMask(mask, m_columns, m_rows, image.size());
  for (size_t i = 0; i < mask.size(); ++i) {
    if (mask[i])
      m_lossy[i] = 1;
  }
  return rects;
}

std::vector<Tile> FrameEncoder::encode(const QImage &source, bool *key)
{
  QImage image = source.format() == QImage::Format_RGB32 ? source : source.convertToFormat(QImage::Format_RGB32);
  const int tile = kTileSize;
  const bool lossless = m_quality >= 100 || !jpegAvailable();
  const Codec moving = lossless ? Codec::Png : Codec::Jpeg;

  std::vector<Tile> tiles;
  const bool isKey = m_reset || image.size() != m_previous.size();
  if (key)
    *key = isKey;

  if (isKey) {
    m_reset = false;
    m_columns = (image.width() + tile - 1) / tile;
    m_rows = (image.height() + tile - 1) / tile;
    const auto count = static_cast<size_t>(m_columns) * m_rows;
    m_lossy.assign(count, lossless ? 0 : 1);
    m_age.assign(count, 0);
    std::vector<uint8_t> all(count, 1);
    for (const auto &rect : rectsFromMask(all, m_columns, m_rows, image.size()))
      tiles.push_back({rect, moving, compress(image, rect, moving)});
    m_previous = image;
    return tiles;
  }

  const auto changed = changedRects(image);
  for (const auto &rect : changed)
    tiles.push_back({rect, moving, compress(image, rect, moving)});
  if (lossless)
    std::fill(m_lossy.begin(), m_lossy.end(), 0);

  // sharpen what has settled, a few tiles at a time, while little else is changing
  if (!lossless && changed.size() < 4) {
    std::vector<uint8_t> refine(m_lossy.size(), 0);
    int budget = kRefineBudget;
    for (size_t i = 0; i < m_lossy.size() && budget > 0; ++i) {
      if (m_lossy[i] && m_age[i] >= kSettledFrames) {
        refine[i] = 1;
        m_lossy[i] = 0;
        --budget;
      }
    }
    if (budget < kRefineBudget) {
      for (const auto &rect : rectsFromMask(refine, m_columns, m_rows, image.size()))
        tiles.push_back({rect, Codec::Png, compress(image, rect, Codec::Png)});
    }
  }

  m_previous = image;
  return tiles;
}

QByteArray FrameEncoder::compress(const QImage &image, const QRect &rect, Codec codec) const
{
  QByteArray data;
  QBuffer buffer(&data);
  buffer.open(QIODevice::WriteOnly);
  const QImage part = image.copy(rect);
  if (codec == Codec::Jpeg) {
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(m_quality);
    writer.write(part);
  } else {
    QImageWriter writer(&buffer, "png");
    writer.setQuality(30); // zlib effort: screens compress well even at low effort, and it is much faster
    writer.write(part);
  }
  return data;
}

QImage decodeTile(const Tile &tile)
{
  QImage image;
  image.loadFromData(tile.data, tile.codec == Codec::Jpeg ? "jpeg" : "png");
  return image;
}

FrameCanvas::FrameCanvas() = default;
FrameCanvas::~FrameCanvas() = default;

bool FrameCanvas::canDecodeH264()
{
  static const bool available = createH264Decoder() != nullptr;
  return available;
}

QString FrameCanvas::decoderName() const
{
  return m_decoder ? m_decoder->name() : QString();
}

bool FrameCanvas::apply(const FrameHeader &header, const std::vector<Tile> &tiles)
{
  if (!tiles.empty() && tiles.front().codec == Codec::H264) {
    if (!m_decoder && !m_decoderMissing) {
      m_decoder = createH264Decoder();
      m_decoderMissing = !m_decoder;
    }
    if (!m_decoder)
      return false;
    bool ok = true;
    for (const auto &tile : tiles) {
      QImage picture;
      if (!m_decoder->decode(tile.data, picture)) {
        ok = false;
        continue;
      }
      if (!picture.isNull())
        m_image = picture.size() == header.size ? picture : picture.copy(QRect(QPoint(0, 0), header.size));
    }
    if (!ok && (header.flags & FrameHeader::kKeyFrame)) {
      m_decoder.reset(); // start again from a clean decoder with the next key frame
    }
    return ok;
  }

  if (header.flags & FrameHeader::kKeyFrame) {
    if (m_image.size() != header.size) {
      m_image = QImage(header.size, QImage::Format_RGB32);
      m_image.fill(Qt::black);
    }
  } else if (m_image.size() != header.size) {
    return false;
  }

  QPainter painter(&m_image);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  for (const auto &tile : tiles) {
    const QImage part = decodeTile(tile);
    if (part.isNull())
      continue;
    painter.drawImage(tile.rect.topLeft(), part, QRect(QPoint(0, 0), tile.rect.size()));
  }
  return true;
}

} // namespace deskflow::remote
