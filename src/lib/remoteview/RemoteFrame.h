/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>

#include <cstdint>
#include <memory>
#include <vector>

/*!
Remote view: a client streams its screen to a server, which shows it in a window.

A frame is either one H.264 access unit covering the whole picture, or (when
either end has no H.264 codec) the set of rectangles that changed since the
previous frame, each compressed on its own: JPEG while things move, PNG to
sharpen what has settled. A frame travels as one or more pieces, each small enough to share the
connection with keyboard and mouse messages. The same piece format is used on
the network and between the server's core and its GUI.
*/
namespace deskflow::remote {

enum class Codec : uint8_t
{
  Jpeg = 0,
  Png = 1, //!< also used for everything when Qt's JPEG plugin is missing
  H264 = 2 //!< one access unit (Annex B) for the whole frame
};

struct Tile
{
  QRect rect; //!< in frame pixels
  Codec codec = Codec::Jpeg;
  QByteArray data;
  bool more = false; //!< the data continues in the next tile (a tile split across pieces)
};

class VideoDecoder;

//! Describes the frame a piece belongs to
struct FrameHeader
{
  static constexpr uint8_t kKeyFrame = 0x01;  //!< the tiles cover the whole frame
  static constexpr uint8_t kLastPiece = 0x02; //!< the frame is complete after this piece

  uint8_t flags = 0;
  uint32_t sequence = 0;
  QRect source; //!< the captured area in the client's screen coordinates
  QSize size;   //!< frame size in pixels (the source, scaled)
  uint8_t display = 0;
  uint8_t displayCount = 1;
};

//! Largest encoded piece, so a frame never holds up input for long
inline constexpr int kMaxPieceBytes = 192 * 1024;

QByteArray writePiece(const FrameHeader &header, const std::vector<Tile> &tiles);
bool readPiece(const QByteArray &data, FrameHeader &header, std::vector<Tile> &tiles);
//! Only the header of a piece, without copying its tiles
bool readHeader(const QByteArray &data, FrameHeader &header);

//! Cut a frame's tiles into pieces of about kMaxPieceBytes, splitting big tiles (H.264 key frames)
std::vector<QByteArray> splitIntoPieces(FrameHeader header, std::vector<Tile> tiles);

//! Collects the pieces of a frame and joins split tiles
class FrameAssembler
{
public:
  //! Add a piece; true once its frame is complete (then see header() and tiles()). Bad pieces drop the frame.
  bool add(const QByteArray &piece);
  const FrameHeader &header() const
  {
    return m_header;
  }
  const std::vector<Tile> &tiles() const
  {
    return m_tiles;
  }

private:
  FrameHeader m_header;
  std::vector<Tile> m_tiles;
  bool m_open = false;
};

//! Turns a stream of screen images into changed rectangles
class FrameEncoder
{
public:
  static constexpr int kTileSize = 64;

  //! JPEG quality 1..100 for changing areas
  void setQuality(int quality)
  {
    m_quality = quality;
  }
  int quality() const
  {
    return m_quality;
  }

  //! Send everything again with the next frame
  void reset();

  //! Changed areas of \c image (any size; a new size is a key frame). \c key is set for a key frame.
  std::vector<Tile> encode(const QImage &image, bool *key = nullptr);

  //! True if areas sent lossily are waiting to be sent again losslessly
  bool hasPendingRefine() const;

private:
  QByteArray compress(const QImage &image, const QRect &rect, Codec codec) const;
  std::vector<QRect> changedRects(const QImage &image);

  QImage m_previous;
  std::vector<uint8_t> m_lossy; //!< per tile: last sent lossily and not refined yet
  std::vector<uint8_t> m_age;   //!< per tile: frames since it last changed (saturating)
  int m_columns = 0;
  int m_rows = 0;
  int m_quality = 80;
  bool m_reset = true;
};

//! Puts received frames together into the picture the client is showing
class FrameCanvas
{
public:
  FrameCanvas();
  ~FrameCanvas();
  FrameCanvas(const FrameCanvas &) = delete;
  FrameCanvas &operator=(const FrameCanvas &) = delete;

  //! Apply a frame (or a piece of a tile frame); false if it can't be (ask for a key frame)
  bool apply(const FrameHeader &header, const std::vector<Tile> &tiles);
  //! Whether this end can decode H.264
  static bool canDecodeH264();
  //! The H.264 decoder in use, for the GUI ("" before the first H.264 frame)
  QString decoderName() const;
  const QImage &image() const
  {
    return m_image;
  }
  void clear()
  {
    m_image = QImage();
  }

private:
  QImage m_image;
  std::unique_ptr<VideoDecoder> m_decoder;
  bool m_decoderMissing = false;
};

QImage decodeTile(const Tile &tile);

} // namespace deskflow::remote
