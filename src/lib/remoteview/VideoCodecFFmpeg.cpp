/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// H.264 through FFmpeg's libavcodec, for platforms without a native codec here (Linux, BSD).

#include "remoteview/VideoCodec.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include <cstring>

namespace deskflow::remote {

namespace {

struct CodecContextDeleter
{
  void operator()(AVCodecContext *context) const
  {
    avcodec_free_context(&context);
  }
};
struct FrameDeleter
{
  void operator()(AVFrame *frame) const
  {
    av_frame_free(&frame);
  }
};
struct PacketDeleter
{
  void operator()(AVPacket *packet) const
  {
    av_packet_free(&packet);
  }
};

using ContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

class FFmpegEncoder : public VideoEncoder
{
public:
  bool open(const QSize &size, int fps, int bitrateKbps) override
  {
    // a hardware encoder needs device setup per vendor; libx264 at its fastest keeps up with a screen
    for (const char *name : {"libx264"}) {
      const AVCodec *codec = avcodec_find_encoder_by_name(name);
      if (codec == nullptr)
        continue;
      ContextPtr context(avcodec_alloc_context3(codec));
      context->width = size.width();
      context->height = size.height();
      context->time_base = AVRational{1, 1000};
      context->framerate = AVRational{fps, 1};
      context->pix_fmt = AV_PIX_FMT_NV12;
      context->bit_rate = static_cast<int64_t>(bitrateKbps) * 1000;
      context->rc_max_rate = context->bit_rate * 2;
      context->rc_buffer_size = static_cast<int>(context->bit_rate / std::max(fps, 1) * 2);
      context->gop_size = 600; // key frames on demand; one now and then repairs a lost picture
      context->max_b_frames = 0;
      context->colorspace = AVCOL_SPC_BT709;
      context->color_primaries = AVCOL_PRI_BT709;
      context->color_trc = AVCOL_TRC_BT709;
      context->color_range = AVCOL_RANGE_MPEG;
      context->flags |= AV_CODEC_FLAG_LOW_DELAY;
      if (std::strcmp(name, "libx264") == 0) {
        av_opt_set(context->priv_data, "preset", "ultrafast", 0);
        av_opt_set(context->priv_data, "tune", "zerolatency", 0);
        av_opt_set(context->priv_data, "profile", "high", 0);
        av_opt_set(context->priv_data, "forced-idr", "1", 0);
      }
      if (avcodec_open2(context.get(), codec, nullptr) < 0)
        continue;
      m_context = std::move(context);
      m_name = QStringLiteral("FFmpeg %1").arg(QString::fromLatin1(name));
      m_frame.reset(av_frame_alloc());
      m_packet.reset(av_packet_alloc());
      m_frame->format = AV_PIX_FMT_NV12;
      m_frame->width = size.width();
      m_frame->height = size.height();
      return av_frame_get_buffer(m_frame.get(), 0) >= 0;
    }
    return false;
  }

  QByteArray encode(const QImage &frame, bool forceKey, bool &isKey) override
  {
    isKey = false;
    if (!m_context || av_frame_make_writable(m_frame.get()) < 0)
      return {};

    toNv12(frame, m_nv12);
    const int width = m_frame->width;
    const int height = m_frame->height;
    for (int row = 0; row < height; ++row)
      std::memcpy(m_frame->data[0] + row * m_frame->linesize[0], m_nv12.constData() + row * width, width);
    const auto *uv = m_nv12.constData() + static_cast<qsizetype>(width) * height;
    for (int row = 0; row < height / 2; ++row)
      std::memcpy(m_frame->data[1] + row * m_frame->linesize[1], uv + row * width, width);

    m_frame->pts = m_pts++;
    m_frame->pict_type = forceKey ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (forceKey)
      m_frame->flags |= AV_FRAME_FLAG_KEY;
    else
      m_frame->flags &= ~AV_FRAME_FLAG_KEY;

    if (avcodec_send_frame(m_context.get(), m_frame.get()) < 0)
      return {};
    QByteArray out;
    while (avcodec_receive_packet(m_context.get(), m_packet.get()) == 0) {
      out.append(reinterpret_cast<const char *>(m_packet->data), m_packet->size);
      isKey = isKey || (m_packet->flags & AV_PKT_FLAG_KEY);
      av_packet_unref(m_packet.get());
    }
    return out;
  }

  QString name() const override
  {
    return m_name;
  }

private:
  ContextPtr m_context;
  FramePtr m_frame;
  PacketPtr m_packet;
  QByteArray m_nv12;
  QString m_name;
  int64_t m_pts = 0;
};

class FFmpegDecoder : public VideoDecoder
{
public:
  bool init()
  {
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec == nullptr)
      return false;
    m_context.reset(avcodec_alloc_context3(codec));
    m_context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_context->thread_count = 1; // frame threading would hold pictures back
    if (avcodec_open2(m_context.get(), codec, nullptr) < 0)
      return false;
    m_frame.reset(av_frame_alloc());
    m_packet.reset(av_packet_alloc());
    return true;
  }

  bool decode(const QByteArray &accessUnit, QImage &picture) override
  {
    if (av_new_packet(m_packet.get(), static_cast<int>(accessUnit.size())) < 0)
      return false;
    std::memcpy(m_packet->data, accessUnit.constData(), accessUnit.size());
    const int sent = avcodec_send_packet(m_context.get(), m_packet.get());
    av_packet_unref(m_packet.get());
    if (sent < 0)
      return false;
    bool ok = true;
    while (true) {
      const int received = avcodec_receive_frame(m_context.get(), m_frame.get());
      if (received == AVERROR(EAGAIN) || received == AVERROR_EOF)
        break;
      if (received < 0) {
        ok = false;
        break;
      }
      picture = convert(m_frame.get());
      ok = !picture.isNull();
      av_frame_unref(m_frame.get());
    }
    return ok;
  }

  QString name() const override
  {
    return QStringLiteral("FFmpeg");
  }

private:
  static QImage convert(const AVFrame *frame)
  {
    if (frame->format == AV_PIX_FMT_NV12)
      return fromNv12(
          frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->width, frame->height
      );
    if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P)
      return {};
    // interleave the chroma planes, then share the NV12 conversion
    const int width = frame->width;
    const int height = frame->height;
    QByteArray uv(static_cast<qsizetype>(width + 1) / 2 * 2 * ((height + 1) / 2), Qt::Uninitialized);
    const int uvStride = (width + 1) / 2 * 2;
    for (int row = 0; row < (height + 1) / 2; ++row) {
      auto *out = reinterpret_cast<uint8_t *>(uv.data()) + row * uvStride;
      const uint8_t *u = frame->data[1] + row * frame->linesize[1];
      const uint8_t *v = frame->data[2] + row * frame->linesize[2];
      for (int col = 0; col < (width + 1) / 2; ++col) {
        out[col * 2] = u[col];
        out[col * 2 + 1] = v[col];
      }
    }
    return fromNv12(
        frame->data[0], frame->linesize[0], reinterpret_cast<const uint8_t *>(uv.constData()), uvStride, width, height
    );
  }

  ContextPtr m_context;
  FramePtr m_frame;
  PacketPtr m_packet;
};

} // namespace

std::unique_ptr<VideoEncoder> createH264Encoder()
{
  return std::make_unique<FFmpegEncoder>();
}

std::unique_ptr<VideoDecoder> createH264Decoder()
{
  auto decoder = std::make_unique<FFmpegDecoder>();
  if (!decoder->init())
    return nullptr;
  return decoder;
}

} // namespace deskflow::remote
