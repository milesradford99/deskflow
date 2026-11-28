/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "remoteview/RemoteFrame.h"

#include <QSize>
#include <QString>

#include <memory>
#include <vector>

namespace deskflow::remote {

class VideoEncoder;

//! Quality presets the viewer can ask for
enum class Quality
{
  Smooth = 0,   //!< smaller picture, lower bit rate
  Balanced = 1, //!< up to 1920x1200
  Sharp = 2     //!< full resolution up to 4K, high bit rate
};

//! The box a preset's pictures fit in
QSize presetMaxSize(Quality quality);

//! The largest picture a preset sends, keeping \c source's aspect; even sides (H.264 needs them)
QSize fitSize(const QSize &source, Quality quality);

//! Encodes the client's screen as H.264 when both ends can, else as tiles
class StreamEncoder
{
public:
  StreamEncoder();
  ~StreamEncoder();
  StreamEncoder(const StreamEncoder &) = delete;
  StreamEncoder &operator=(const StreamEncoder &) = delete;

  //! \c allowH264: the viewer can decode it. Changes take effect with a key frame.
  void configure(bool allowH264, Quality quality, int fps);
  void requestKeyFrame();

  //! The next frame's tiles (empty if nothing changed); \c key is set for a key frame
  std::vector<Tile> encode(const QImage &image, bool &key);

  //! Tiles wait to be sharpened, or a key frame is due: call encode() with the last picture again
  bool hasIdleWork() const;

  bool usingH264() const
  {
    return m_video != nullptr;
  }
  //! e.g. "H.264 · VideoToolbox" or "JPEG"
  QString codecName() const;

private:
  std::vector<Tile> encodeTiles(const QImage &image, bool &key);

  bool m_allowH264 = true;
  bool m_h264Failed = false;
  Quality m_quality = Quality::Balanced;
  int m_fps = 30;
  bool m_wantKey = true;
  std::unique_ptr<VideoEncoder> m_video;
  QSize m_videoSize;
  FrameEncoder m_tiles;
};

} // namespace deskflow::remote
