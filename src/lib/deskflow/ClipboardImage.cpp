/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardImage.h"

#include "base/Log.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QtEndian>

#include <cstring>

namespace deskflow::clipboardImage {

namespace {

constexpr quint32 kBmpFileHeaderSize = 14;
constexpr quint32 kInfoHeaderSize = 40;
constexpr quint32 kCoreHeaderSize = 12;
constexpr quint32 kBiBitfields = 3;
constexpr quint32 kBiAlphaBitfields = 6;

//! Offset from the start of the DIB to its pixel data, or 0 if malformed.
quint32 dibPixelOffset(const uchar *dib, qsizetype size)
{
  if (size < 4)
    return 0;

  const auto headerSize = qFromLittleEndian<quint32>(dib);
  if (headerSize < kCoreHeaderSize || headerSize > size)
    return 0;

  // OS/2 core header: 3 byte palette entries, no compression or colour count fields
  if (headerSize == kCoreHeaderSize) {
    const auto bits = qFromLittleEndian<quint16>(dib + 10);
    return headerSize + (bits <= 8 ? (1u << bits) * 3 : 0);
  }

  if (headerSize < kInfoHeaderSize)
    return 0;

  const auto bits = qFromLittleEndian<quint16>(dib + 14);
  const auto compression = qFromLittleEndian<quint32>(dib + 16);
  auto colors = qFromLittleEndian<quint32>(dib + 32);
  if (colors == 0 && bits <= 8)
    colors = 1u << bits;
  if (colors > 256)
    return 0;

  quint32 offset = headerSize + colors * 4;
  // the masks follow a plain info header, but are part of the larger V4/V5 headers
  if (headerSize == kInfoHeaderSize) {
    if (compression == kBiBitfields)
      offset += 12;
    else if (compression == kBiAlphaBitfields)
      offset += 16;
  }
  return offset;
}

} // namespace

bool isPng(const std::string &data)
{
  static const char kSignature[] = "\x89PNG\r\n\x1a\n";
  return data.size() > 8 && std::memcmp(data.data(), kSignature, 8) == 0;
}

std::string trimPng(const std::string &data)
{
  if (!isPng(data))
    return {};

  // signature, then chunks of: length (4, big endian), type (4), data, crc (4)
  const auto *raw = reinterpret_cast<const uchar *>(data.data());
  size_t offset = 8;
  while (data.size() - offset >= 12) {
    const size_t length = qFromBigEndian<quint32>(raw + offset);
    if (length > data.size() - offset - 12)
      return {};

    const bool isEnd = std::memcmp(raw + offset + 4, "IEND", 4) == 0;
    offset += 12 + length;
    if (isEnd)
      return data.substr(0, offset);
  }
  return {};
}

std::string dibToPng(const std::string &dib)
{
  const auto *raw = reinterpret_cast<const uchar *>(dib.data());
  const auto pixelOffset = dibPixelOffset(raw, static_cast<qsizetype>(dib.size()));
  if (pixelOffset == 0 || pixelOffset > dib.size()) {
    LOG_DEBUG("clipboard dib is malformed, size: %zu bytes", dib.size());
    return {};
  }

  QByteArray bmp;
  bmp.resize(kBmpFileHeaderSize);
  auto *header = reinterpret_cast<uchar *>(bmp.data());
  header[0] = 'B';
  header[1] = 'M';
  qToLittleEndian<quint32>(static_cast<quint32>(kBmpFileHeaderSize + dib.size()), header + 2);
  qToLittleEndian<quint32>(0, header + 6);
  qToLittleEndian<quint32>(kBmpFileHeaderSize + pixelOffset, header + 10);
  bmp.append(dib.data(), static_cast<qsizetype>(dib.size()));

  QImage image;
  if (!image.loadFromData(bmp, "BMP")) {
    LOG_DEBUG("failed to decode clipboard dib");
    return {};
  }

  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  // Quality maps to zlib level; the default is slow on screenshots for a few percent gain.
  if (!image.save(&buffer, "PNG", 60)) {
    LOG_DEBUG("failed to encode clipboard image as png");
    return {};
  }
  return png.toStdString();
}

std::string pngToDib(const std::string &png)
{
  QImage source;
  if (!source.loadFromData(reinterpret_cast<const uchar *>(png.data()), static_cast<int>(png.size()), "PNG")) {
    LOG_DEBUG("failed to decode clipboard png");
    return {};
  }

  QImage image(source.size(), QImage::Format_RGB32);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    painter.drawImage(0, 0, source);
  }

  const auto width = static_cast<quint32>(image.width());
  const auto height = static_cast<quint32>(image.height());
  const auto rowBytes = static_cast<size_t>(width) * 4;

  std::string dib(kInfoHeaderSize + rowBytes * height, '\0');
  auto *out = reinterpret_cast<uchar *>(dib.data());
  qToLittleEndian<quint32>(kInfoHeaderSize, out);
  qToLittleEndian<quint32>(width, out + 4);
  qToLittleEndian<quint32>(height, out + 8); // positive: bottom-up
  qToLittleEndian<quint16>(1, out + 12);
  qToLittleEndian<quint16>(32, out + 14);
  qToLittleEndian<quint32>(0, out + 16); // BI_RGB
  qToLittleEndian<quint32>(static_cast<quint32>(rowBytes * height), out + 20);
  qToLittleEndian<quint32>(static_cast<quint32>(source.dotsPerMeterX()), out + 24);
  qToLittleEndian<quint32>(static_cast<quint32>(source.dotsPerMeterY()), out + 28);

  out += kInfoHeaderSize;
  for (quint32 y = 0; y < height; ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(static_cast<int>(height - 1 - y)));
    for (quint32 x = 0; x < width; ++x) {
      // QRgb is 0xffRRGGBB; a DIB stores B, G, R, reserved
      qToLittleEndian<quint32>(line[x], out);
      out += 4;
    }
  }
  return dib;
}

} // namespace deskflow::clipboardImage
