/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "remoteview/VideoCodec.h"

#include <algorithm>

namespace deskflow::remote {

namespace {

inline uint8_t clamp8(int v)
{
  return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

} // namespace

// Fixed point BT.709 limited range, the coefficients scaled by 256.
void toNv12(const QImage &source, QByteArray &out)
{
  const QImage frame = source.format() == QImage::Format_RGB32 || source.format() == QImage::Format_ARGB32
                           ? source
                           : source.convertToFormat(QImage::Format_RGB32);
  const int width = frame.width() & ~1;
  const int height = frame.height() & ~1;
  out.resize(static_cast<qsizetype>(width) * height * 3 / 2);
  auto *y = reinterpret_cast<uint8_t *>(out.data());
  auto *uv = y + static_cast<qsizetype>(width) * height;

  for (int row = 0; row < height; row += 2) {
    const auto *line0 = reinterpret_cast<const QRgb *>(frame.constScanLine(row));
    const auto *line1 = reinterpret_cast<const QRgb *>(frame.constScanLine(row + 1));
    uint8_t *y0 = y + static_cast<qsizetype>(row) * width;
    uint8_t *y1 = y0 + width;
    uint8_t *uvRow = uv + static_cast<qsizetype>(row / 2) * width;
    for (int col = 0; col < width; col += 2) {
      int sumR = 0;
      int sumG = 0;
      int sumB = 0;
      const QRgb pixels[4] = {line0[col], line0[col + 1], line1[col], line1[col + 1]};
      uint8_t *targets[4] = {y0 + col, y0 + col + 1, y1 + col, y1 + col + 1};
      for (int i = 0; i < 4; ++i) {
        const int r = qRed(pixels[i]);
        const int g = qGreen(pixels[i]);
        const int b = qBlue(pixels[i]);
        *targets[i] = clamp8(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
        sumR += r;
        sumG += g;
        sumB += b;
      }
      const int r = sumR / 4;
      const int g = sumG / 4;
      const int b = sumB / 4;
      uvRow[col] = clamp8(((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128);
      uvRow[col + 1] = clamp8(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
    }
  }
}

QImage fromNv12(const uint8_t *y, int yStride, const uint8_t *uv, int uvStride, int width, int height)
{
  QImage image(width, height, QImage::Format_RGB32);
  for (int row = 0; row < height; ++row) {
    auto *line = reinterpret_cast<QRgb *>(image.scanLine(row));
    const uint8_t *yRow = y + static_cast<qsizetype>(row) * yStride;
    const uint8_t *uvRow = uv + static_cast<qsizetype>(row / 2) * uvStride;
    for (int col = 0; col < width; ++col) {
      const int c = (yRow[col] - 16) * 298;
      const int d = uvRow[col & ~1] - 128;
      const int e = uvRow[(col & ~1) + 1] - 128;
      line[col] = qRgb(
          clamp8((c + 459 * e + 128) >> 8), clamp8((c - 55 * d - 136 * e + 128) >> 8), clamp8((c + 541 * d + 128) >> 8)
      );
    }
  }
  return image;
}

} // namespace deskflow::remote
