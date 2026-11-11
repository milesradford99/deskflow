/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardImageTests.h"

#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardImage.h"

#include <QBuffer>
#include <QImage>
#include <QtEndian>

using namespace deskflow::clipboardImage;
using Format = IClipboard::Format;

namespace {

std::string toPng(const QImage &image)
{
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes.toStdString();
}

QImage fromPng(const std::string &png)
{
  QImage image;
  image.loadFromData(reinterpret_cast<const uchar *>(png.data()), static_cast<int>(png.size()), "PNG");
  return image;
}

QImage testImage()
{
  QImage image(3, 2, QImage::Format_RGB32);
  image.setPixel(0, 0, qRgb(255, 0, 0));
  image.setPixel(1, 0, qRgb(0, 255, 0));
  image.setPixel(2, 0, qRgb(0, 0, 255));
  image.setPixel(0, 1, qRgb(10, 20, 30));
  image.setPixel(1, 1, qRgb(40, 50, 60));
  image.setPixel(2, 1, qRgb(70, 80, 90));
  return image;
}

void fill(Clipboard &clipboard, Format format, const std::string &data)
{
  clipboard.open(0);
  clipboard.add(format, data);
  clipboard.close();
}

} // namespace

void ClipboardImageTests::isPng_detectsSignature()
{
  QVERIFY(isPng(toPng(testImage())));
  QVERIFY(!isPng("BM not a png"));
  QVERIFY(!isPng(""));
}

void ClipboardImageTests::trimPng_dropsTrailingBytes()
{
  const auto png = toPng(testImage());
  QCOMPARE(trimPng(png + std::string(37, '\0')), png);
  QCOMPARE(trimPng(png), png);
}

void ClipboardImageTests::trimPng_rejectsTruncated()
{
  const auto png = toPng(testImage());
  QVERIFY(trimPng(png.substr(0, png.size() - 5)).empty());
  QVERIFY(trimPng("garbage").empty());
}

void ClipboardImageTests::pngToDib_writesBottomUpBgr()
{
  const auto dib = pngToDib(toPng(testImage()));
  const auto *raw = reinterpret_cast<const uchar *>(dib.data());

  QCOMPARE(dib.size(), size_t(40 + 3 * 2 * 4));
  QCOMPARE(qFromLittleEndian<quint32>(raw), 40u);
  QCOMPARE(qFromLittleEndian<qint32>(raw + 4), 3);
  QCOMPARE(qFromLittleEndian<qint32>(raw + 8), 2);
  QCOMPARE(qFromLittleEndian<quint16>(raw + 14), quint16(32));
  QCOMPARE(qFromLittleEndian<quint32>(raw + 16), 0u);

  // first stored row is the bottom image row, pixels are B, G, R
  QCOMPARE(raw[40], uchar(30));
  QCOMPARE(raw[41], uchar(20));
  QCOMPARE(raw[42], uchar(10));
  // top-left red pixel starts the second stored row
  QCOMPARE(raw[40 + 12], uchar(0));
  QCOMPARE(raw[40 + 14], uchar(255));
}

void ClipboardImageTests::pngToDib_flattensTransparencyOntoWhite()
{
  QImage image(1, 1, QImage::Format_ARGB32);
  image.fill(Qt::transparent);

  const auto dib = pngToDib(toPng(image));
  const auto *raw = reinterpret_cast<const uchar *>(dib.data());
  QCOMPARE(raw[40], uchar(255));
  QCOMPARE(raw[41], uchar(255));
  QCOMPARE(raw[42], uchar(255));
}

void ClipboardImageTests::dibToPng_roundTripsPixels()
{
  const auto source = testImage();
  const auto png = dibToPng(pngToDib(toPng(source)));

  QVERIFY(isPng(png));
  QCOMPARE(fromPng(png).convertToFormat(QImage::Format_RGB32), source);
}

void ClipboardImageTests::dibToPng_rejectsGarbage()
{
  QVERIFY(dibToPng("").empty());
  QVERIFY(dibToPng("xx").empty());
  QVERIFY(dibToPng(std::string(64, '\x7f')).empty());
}

void ClipboardImageTests::marshall_sendsLoneBitmapAsPng()
{
  Clipboard source;
  fill(source, Format::Bitmap, pngToDib(toPng(testImage())));

  Clipboard received;
  received.unmarshall(source.marshall(), 0);
  received.open(0);
  QVERIFY(received.has(Format::Png));
  QVERIFY(!received.has(Format::Bitmap));
  QCOMPARE(fromPng(received.get(Format::Png)).convertToFormat(QImage::Format_RGB32), testImage());
  received.close();
}

void ClipboardImageTests::marshall_prefersPngOverBitmap()
{
  const auto png = toPng(testImage());
  Clipboard source;
  source.open(0);
  source.add(Format::Bitmap, pngToDib(png));
  source.add(Format::Png, png);
  source.close();

  Clipboard received;
  received.unmarshall(source.marshall(), 0);
  received.open(0);
  QVERIFY(!received.has(Format::Bitmap));
  QCOMPARE(received.get(Format::Png), png);
  received.close();
}

void ClipboardImageTests::copy_skipsBitmapWhenPngPresent()
{
  const auto png = toPng(testImage());
  Clipboard source;
  source.open(0);
  source.add(Format::Bitmap, pngToDib(png));
  source.add(Format::Png, png);
  source.add(Format::Text, "caption");
  source.close();

  Clipboard copy;
  QVERIFY(IClipboard::copy(&copy, &source));
  copy.open(0);
  QVERIFY(copy.has(Format::Png));
  QVERIFY(copy.has(Format::Text));
  QVERIFY(!copy.has(Format::Bitmap));
  copy.close();
}

QTEST_MAIN(ClipboardImageTests)
