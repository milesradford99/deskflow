/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RemoteFrameTests.h"

#include "remoteview/RemoteFrame.h"
#include "remoteview/StreamEncoder.h"
#include "remoteview/VideoCodec.h"

#include <QPainter>

using namespace deskflow::remote;

namespace {

QImage screen(int width = 300, int height = 200)
{
  QImage image(width, height, QImage::Format_RGB32);
  image.fill(QColor(240, 240, 240));
  QPainter painter(&image);
  painter.fillRect(20, 20, 100, 60, QColor(90, 85, 214));
  return image;
}

int area(const std::vector<Tile> &tiles)
{
  int total = 0;
  for (const auto &tile : tiles)
    total += tile.rect.width() * tile.rect.height();
  return total;
}

} // namespace

void RemoteFrameTests::firstFrame_isKeyAndCoversEverything()
{
  FrameEncoder encoder;
  bool key = false;
  const auto tiles = encoder.encode(screen(), &key);
  QVERIFY(key);
  QCOMPARE(area(tiles), 300 * 200);
}

void RemoteFrameTests::unchangedFrame_sendsNothingOnceRefined()
{
  FrameEncoder encoder;
  encoder.setQuality(100); // lossless: nothing left to sharpen
  encoder.encode(screen());
  bool key = true;
  QVERIFY(encoder.encode(screen(), &key).empty());
  QVERIFY(!key);
  QVERIFY(!encoder.hasPendingRefine());
}

void RemoteFrameTests::smallChange_sendsOnlyThatArea()
{
  FrameEncoder encoder;
  encoder.setQuality(100);
  encoder.encode(screen());
  auto next = screen();
  next.setPixel(150, 150, qRgb(255, 0, 0));
  const auto tiles = encoder.encode(next);
  QCOMPARE(tiles.size(), size_t(1));
  QVERIFY(tiles[0].rect.contains(150, 150));
  QCOMPARE(tiles[0].rect, QRect(128, 128, 64, 64));
}

void RemoteFrameTests::pieces_roundTripAndRebuildPicture()
{
  FrameEncoder encoder;
  encoder.setQuality(100);
  FrameCanvas canvas;

  FrameHeader header;
  header.source = QRect(-1280, 0, 1280, 800);
  header.size = QSize(300, 200);
  header.flags = FrameHeader::kKeyFrame;
  header.sequence = 7;
  header.display = 1;
  header.displayCount = 2;

  auto pieces = splitIntoPieces(header, encoder.encode(screen()));
  QVERIFY(!pieces.empty());
  for (size_t i = 0; i < pieces.size(); ++i) {
    FrameHeader read;
    std::vector<Tile> tiles;
    QVERIFY(readPiece(pieces[i], read, tiles));
    QCOMPARE(read.sequence, 7u);
    QCOMPARE(read.source, header.source);
    QCOMPARE(read.display, uint8_t(1));
    QCOMPARE(read.displayCount, uint8_t(2));
    QCOMPARE(bool(read.flags & FrameHeader::kLastPiece), i + 1 == pieces.size());
    QVERIFY(canvas.apply(read, tiles));
  }
  QCOMPARE(canvas.image(), screen());

  auto next = screen();
  QPainter(&next).fillRect(200, 100, 30, 30, Qt::red);
  header.flags = 0;
  pieces = splitIntoPieces(header, encoder.encode(next));
  for (const auto &piece : pieces) {
    FrameHeader read;
    std::vector<Tile> tiles;
    QVERIFY(readPiece(piece, read, tiles));
    QVERIFY(canvas.apply(read, tiles));
  }
  QCOMPARE(canvas.image(), next);
}

void RemoteFrameTests::settledArea_isSharpenedLosslessly()
{
  FrameEncoder encoder;
  encoder.setQuality(50);
  encoder.encode(screen());
  QVERIFY(encoder.hasPendingRefine());

  for (int i = 0; i < 10 && encoder.hasPendingRefine(); ++i) {
    const auto tiles = encoder.encode(screen());
    for (const auto &tile : tiles)
      QCOMPARE(tile.codec, Codec::Png);
  }
  QVERIFY(!encoder.hasPendingRefine());
}

void RemoteFrameTests::readPiece_rejectsTilesOutsideFrame()
{
  FrameHeader header;
  header.size = QSize(64, 64);
  Tile tile;
  tile.rect = QRect(32, 32, 64, 64);
  tile.codec = Codec::Png;
  tile.data = "x";
  const auto piece = writePiece(header, {tile});
  FrameHeader read;
  std::vector<Tile> tiles;
  QVERIFY(!readPiece(piece, read, tiles));
  QVERIFY(!readPiece(piece.left(10), read, tiles));
}

void RemoteFrameTests::bigTile_isSplitAndJoinedAgain()
{
  FrameHeader header;
  header.size = QSize(64, 64);
  header.sequence = 3;
  header.flags = FrameHeader::kKeyFrame;
  Tile tile;
  tile.rect = QRect(0, 0, 64, 64);
  tile.codec = Codec::H264;
  tile.data = QByteArray(kMaxPieceBytes * 2 + 100, 'x');
  tile.data[kMaxPieceBytes + 5] = 'y';
  const auto pieces = splitIntoPieces(header, {tile});
  QCOMPARE(pieces.size(), size_t(3));

  FrameAssembler assembler;
  QVERIFY(!assembler.add(pieces[0]));
  QVERIFY(!assembler.add(pieces[1]));
  QVERIFY(assembler.add(pieces[2]));
  QCOMPARE(assembler.tiles().size(), size_t(1));
  QCOMPARE(assembler.tiles()[0].data, tile.data);
  QVERIFY(assembler.header().flags & FrameHeader::kKeyFrame);
}

void RemoteFrameTests::h264_roundTripsThroughPiecesAndCanvas()
{
  if (!createH264Encoder() || !FrameCanvas::canDecodeH264())
    QSKIP("built without an H.264 codec");

  StreamEncoder encoder;
  encoder.configure(true, Quality::Balanced, 30);
  FrameCanvas canvas;
  FrameAssembler assembler;

  // 1: key frame shown, 0: delta shown, -1: failed
  const auto send = [&](const QImage &image, uint32_t sequence) -> int {
    bool key = false;
    auto tiles = encoder.encode(image, key);
    FrameHeader header;
    header.size = image.size();
    header.sequence = sequence;
    header.flags = key ? FrameHeader::kKeyFrame : 0;
    bool done = false;
    for (const auto &piece : splitIntoPieces(header, std::move(tiles)))
      done = assembler.add(piece);
    if (!done || !canvas.apply(assembler.header(), assembler.tiles()))
      return -1;
    return key ? 1 : 0;
  };

  QCOMPARE(send(screen(320, 240), 1), 1);
  QVERIFY(encoder.usingH264());
  auto next = screen(320, 240);
  QPainter(&next).fillRect(200, 100, 60, 60, Qt::red);
  QCOMPARE(send(next, 2), 0);
  QCOMPARE(canvas.image().size(), QSize(320, 240));
  const QColor inRed = canvas.image().pixelColor(230, 130);
  QVERIFY2(inRed.red() > 200 && inRed.green() < 60 && inRed.blue() < 60, qPrintable(inRed.name()));
  const QColor inIris = canvas.image().pixelColor(70, 50);
  QVERIFY2(std::abs(inIris.blue() - 214) < 20 && std::abs(inIris.red() - 90) < 20, qPrintable(inIris.name()));
}

void RemoteFrameTests::fitSize_keepsAspectAndEvenSides()
{
  QCOMPARE(fitSize(QSize(5120, 2880), Quality::Balanced), QSize(1920, 1080));
  QCOMPARE(fitSize(QSize(1366, 768), Quality::Sharp), QSize(1366, 768));
  QCOMPARE(fitSize(QSize(1365, 767), Quality::Sharp), QSize(1364, 766));
}

QTEST_MAIN(RemoteFrameTests)
