/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>

/*!
H.264 for the remote view, using the platform's hardware codec where there is one:
VideoToolbox on macOS, Media Foundation on Windows, FFmpeg elsewhere (when built
with it). Streams are Annex B, BT.709 limited range, no B-frames, one access unit
out for each picture in, so nothing waits in the codec.
*/
namespace deskflow::remote {

class VideoEncoder
{
public:
  virtual ~VideoEncoder() = default;

  //! Prepare for pictures of \c size (even width and height)
  virtual bool open(const QSize &size, int fps, int bitrateKbps) = 0;

  //! One access unit for \c frame; empty on failure. \c isKey is set for an IDR picture.
  virtual QByteArray encode(const QImage &frame, bool forceKey, bool &isKey) = 0;

  //! What does the encoding, for logs and the GUI (e.g. "VideoToolbox")
  virtual QString name() const = 0;
};

class VideoDecoder
{
public:
  virtual ~VideoDecoder() = default;

  //! Decode one access unit into \c picture. False on a decoding error (ask for a key frame).
  virtual bool decode(const QByteArray &accessUnit, QImage &picture) = 0;

  virtual QString name() const = 0;
};

//! nullptr if this build has no H.264 encoder
std::unique_ptr<VideoEncoder> createH264Encoder();

//! nullptr if this build has no H.264 decoder
std::unique_ptr<VideoDecoder> createH264Decoder();

//! @name Pixel helpers shared by the implementations (BT.709, limited range)
//@{
//! \c frame (any format) to NV12 in \c out: a Y plane then interleaved UV, both with \c width bytes per row
void toNv12(const QImage &frame, QByteArray &out);
//! NV12 with the given row strides (bytes) to an RGB32 image
QImage fromNv12(const uint8_t *y, int yStride, const uint8_t *uv, int uvStride, int width, int height);
//@}

} // namespace deskflow::remote
