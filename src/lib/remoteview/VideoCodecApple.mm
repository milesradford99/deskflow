/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// H.264 through VideoToolbox: the Mac's hardware encoder and decoder.

#include "remoteview/VideoCodec.h"

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace deskflow::remote {

namespace {

const char kStartCode[4] = {0, 0, 0, 1};

void setNumber(VTSessionRef session, CFStringRef key, int value)
{
  CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
  VTSessionSetProperty(session, key, number);
  CFRelease(number);
}

//! NAL units of an Annex B stream, without their start codes
std::vector<QByteArray> splitAnnexB(const QByteArray &stream)
{
  std::vector<QByteArray> units;
  const auto *data = reinterpret_cast<const uint8_t *>(stream.constData());
  const qsizetype size = stream.size();
  qsizetype start = -1;
  qsizetype i = 0;
  while (i + 2 < size) {
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      if (start >= 0) {
        qsizetype end = i;
        while (end > start && data[end - 1] == 0)
          --end; // the zero of a four byte start code, or trailing zeros
        units.push_back(stream.mid(start, end - start));
      }
      i += 3;
      start = i;
    } else {
      ++i;
    }
  }
  if (start >= 0 && start < size)
    units.push_back(stream.mid(start));
  return units;
}

class VTEncoder : public VideoEncoder
{
public:
  ~VTEncoder() override
  {
    close();
  }

  bool open(const QSize &size, int fps, int bitrateKbps) override
  {
    close();
    m_size = size;
    m_fps = std::max(fps, 1);

    const void *pixelKeys[] = {kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferWidthKey, kCVPixelBufferHeightKey};
    int format = kCVPixelFormatType_32BGRA;
    int width = size.width();
    int height = size.height();
    CFNumberRef numbers[] = {
        CFNumberCreate(nullptr, kCFNumberIntType, &format), CFNumberCreate(nullptr, kCFNumberIntType, &width),
        CFNumberCreate(nullptr, kCFNumberIntType, &height)
    };
    CFDictionaryRef pixels = CFDictionaryCreate(
        nullptr, pixelKeys, reinterpret_cast<const void **>(numbers), 3, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    for (auto *number : numbers)
      CFRelease(number);

    // low latency rate control first (frames come out at once, sized evenly); plain hardware otherwise
    for (int attempt = 0; attempt < 2 && m_session == nullptr; ++attempt) {
      CFMutableDictionaryRef spec =
          CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      CFDictionarySetValue(spec, kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
      if (attempt == 0)
        CFDictionarySetValue(spec, kVTVideoEncoderSpecification_EnableLowLatencyRateControl, kCFBooleanTrue);
      const OSStatus status = VTCompressionSessionCreate(
          nullptr, width, height, kCMVideoCodecType_H264, spec, pixels, nullptr, &VTEncoder::output, this, &m_session
      );
      CFRelease(spec);
      if (status != noErr)
        m_session = nullptr;
    }
    CFRelease(pixels);
    if (m_session == nullptr)
      return false;

    VTSessionSetProperty(m_session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(m_session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(m_session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
    setNumber(m_session, kVTCompressionPropertyKey_AverageBitRate, bitrateKbps * 1000);
    setNumber(m_session, kVTCompressionPropertyKey_ExpectedFrameRate, m_fps);
    setNumber(m_session, kVTCompressionPropertyKey_MaxKeyFrameInterval, 600);
    VTSessionSetProperty(m_session, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
    VTSessionSetProperty(
        m_session, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2
    );
    VTSessionSetProperty(m_session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);
    VTCompressionSessionPrepareToEncodeFrames(m_session);
    return true;
  }

  QByteArray encode(const QImage &source, bool forceKey, bool &isKey) override
  {
    isKey = false;
    if (m_session == nullptr)
      return {};
    const QImage frame =
        source.format() == QImage::Format_RGB32 ? source : source.convertToFormat(QImage::Format_RGB32);

    CVPixelBufferRef buffer = nullptr;
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(m_session);
    if (pool == nullptr || CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &buffer) != kCVReturnSuccess)
      return {};
    CVPixelBufferLockBaseAddress(buffer, 0);
    auto *base = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    const int rows = std::min<int>(frame.height(), static_cast<int>(CVPixelBufferGetHeight(buffer)));
    const size_t rowBytes = std::min<size_t>(static_cast<size_t>(frame.width()) * 4, CVPixelBufferGetWidth(buffer) * 4);
    for (int row = 0; row < rows; ++row)
      std::memcpy(base + row * stride, frame.constScanLine(row), rowBytes);
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    CFDictionaryRef options = nullptr;
    if (forceKey) {
      const void *keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
      const void *values[] = {kCFBooleanTrue};
      options = CFDictionaryCreate(
          nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
      );
    }

    m_out.clear();
    m_outKey = false;
    const OSStatus status = VTCompressionSessionEncodeFrame(
        m_session, buffer, CMTimeMake(m_frameIndex++, m_fps), kCMTimeInvalid, options, nullptr, nullptr
    );
    if (options != nullptr)
      CFRelease(options);
    CVPixelBufferRelease(buffer);
    if (status != noErr)
      return {};
    VTCompressionSessionCompleteFrames(m_session, kCMTimeInvalid); // the output callback has run when this returns
    isKey = m_outKey;
    return m_out;
  }

  QString name() const override
  {
    return QStringLiteral("VideoToolbox");
  }

private:
  void close()
  {
    if (m_session != nullptr) {
      VTCompressionSessionInvalidate(m_session);
      CFRelease(m_session);
      m_session = nullptr;
    }
  }

  static void output(void *refcon, void *, OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sample)
  {
    auto *self = static_cast<VTEncoder *>(refcon);
    if (status != noErr || sample == nullptr || !CMSampleBufferDataIsReady(sample))
      return;

    bool key = true;
    if (CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        attachments != nullptr && CFArrayGetCount(attachments) > 0) {
      auto *attachment = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
      key = !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
    }

    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample);
    int lengthSize = 4;
    if (key) {
      size_t count = 0;
      CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &lengthSize);
      for (size_t i = 0; i < count; ++i) {
        const uint8_t *set = nullptr;
        size_t setSize = 0;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &set, &setSize, nullptr, nullptr) == noErr) {
          self->m_out.append(kStartCode, 4);
          self->m_out.append(reinterpret_cast<const char *>(set), static_cast<qsizetype>(setSize));
        }
      }
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    const size_t total = CMBlockBufferGetDataLength(block);
    QByteArray data(static_cast<qsizetype>(total), Qt::Uninitialized);
    if (CMBlockBufferCopyDataBytes(block, 0, total, data.data()) != kCMBlockBufferNoErr)
      return;

    // length-prefixed NAL units to Annex B
    size_t offset = 0;
    while (offset + lengthSize <= total) {
      uint32_t length = 0;
      for (int i = 0; i < lengthSize; ++i)
        length = (length << 8) | static_cast<uint8_t>(data[static_cast<qsizetype>(offset + i)]);
      offset += lengthSize;
      if (offset + length > total)
        break;
      self->m_out.append(kStartCode, 4);
      self->m_out.append(data.constData() + offset, static_cast<qsizetype>(length));
      offset += length;
    }
    self->m_outKey = key;
  }

  VTCompressionSessionRef m_session = nullptr;
  QSize m_size;
  int m_fps = 30;
  int64_t m_frameIndex = 0;
  QByteArray m_out;
  bool m_outKey = false;
};

class VTDecoder : public VideoDecoder
{
public:
  ~VTDecoder() override
  {
    close();
  }

  bool decode(const QByteArray &accessUnit, QImage &picture) override
  {
    QByteArray avcc;
    bool parametersChanged = false;
    for (const auto &unit : splitAnnexB(accessUnit)) {
      if (unit.isEmpty())
        continue;
      const int type = static_cast<uint8_t>(unit[0]) & 0x1f;
      if (type == 7) {
        parametersChanged = parametersChanged || unit != m_sps;
        m_sps = unit;
      } else if (type == 8) {
        parametersChanged = parametersChanged || unit != m_pps;
        m_pps = unit;
      } else if (type == 9) {
        // access unit delimiter: not needed by VideoToolbox
      } else {
        const auto length = static_cast<uint32_t>(unit.size());
        const char prefix[4] = {
            char(length >> 24), char((length >> 16) & 0xff), char((length >> 8) & 0xff), char(length & 0xff)
        };
        avcc.append(prefix, 4);
        avcc.append(unit);
      }
    }

    if ((parametersChanged || m_session == nullptr) && !m_sps.isEmpty() && !m_pps.isEmpty()) {
      if (!createSession())
        return false;
    }
    if (m_session == nullptr || avcc.isEmpty())
      return m_session != nullptr || m_sps.isEmpty(); // waiting for a key frame is not an error

    CMBlockBufferRef block = nullptr;
    if (CMBlockBufferCreateWithMemoryBlock(
            nullptr, nullptr, static_cast<size_t>(avcc.size()), nullptr, nullptr, 0, static_cast<size_t>(avcc.size()),
            0, &block
        ) != kCMBlockBufferNoErr)
      return false;
    CMBlockBufferReplaceDataBytes(avcc.constData(), block, 0, static_cast<size_t>(avcc.size()));

    CMSampleBufferRef sample = nullptr;
    const size_t sampleSize = static_cast<size_t>(avcc.size());
    const OSStatus created =
        CMSampleBufferCreateReady(nullptr, block, m_format, 1, 0, nullptr, 1, &sampleSize, &sample);
    CFRelease(block);
    if (created != noErr)
      return false;

    m_picture = QImage();
    m_failed = false;
    VTDecodeInfoFlags info = 0;
    const OSStatus status = VTDecompressionSessionDecodeFrame(m_session, sample, 0, this, &info);
    VTDecompressionSessionWaitForAsynchronousFrames(m_session);
    CFRelease(sample);
    if (status != noErr || m_failed) {
      if (status == kVTInvalidSessionErr)
        close(); // e.g. after sleep; rebuilt from the next key frame
      return false;
    }
    if (!m_picture.isNull())
      picture = m_picture;
    return true;
  }

  QString name() const override
  {
    return QStringLiteral("VideoToolbox");
  }

private:
  bool createSession()
  {
    close();
    const uint8_t *sets[] = {
        reinterpret_cast<const uint8_t *>(m_sps.constData()), reinterpret_cast<const uint8_t *>(m_pps.constData())
    };
    const size_t sizes[] = {static_cast<size_t>(m_sps.size()), static_cast<size_t>(m_pps.size())};
    if (CMVideoFormatDescriptionCreateFromH264ParameterSets(nullptr, 2, sets, sizes, 4, &m_format) != noErr) {
      m_format = nullptr;
      return false;
    }

    const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
    int format = kCVPixelFormatType_32BGRA;
    CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &format);
    const void *values[] = {number};
    CFDictionaryRef attributes =
        CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(number);

    VTDecompressionOutputCallbackRecord callback{&VTDecoder::output, this};
    const OSStatus status = VTDecompressionSessionCreate(nullptr, m_format, nullptr, attributes, &callback, &m_session);
    CFRelease(attributes);
    if (status != noErr) {
      m_session = nullptr;
      return false;
    }
    return true;
  }

  void close()
  {
    if (m_session != nullptr) {
      VTDecompressionSessionInvalidate(m_session);
      CFRelease(m_session);
      m_session = nullptr;
    }
    if (m_format != nullptr) {
      CFRelease(m_format);
      m_format = nullptr;
    }
  }

  static void output(void *refcon, void *, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef image, CMTime, CMTime)
  {
    auto *self = static_cast<VTDecoder *>(refcon);
    if (status != noErr || image == nullptr) {
      self->m_failed = status != noErr;
      return;
    }
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    const auto width = static_cast<int>(CVPixelBufferGetWidth(image));
    const auto height = static_cast<int>(CVPixelBufferGetHeight(image));
    const size_t stride = CVPixelBufferGetBytesPerRow(image);
    const auto *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(image));
    QImage picture(width, height, QImage::Format_RGB32);
    for (int row = 0; row < height; ++row)
      std::memcpy(picture.scanLine(row), base + row * stride, static_cast<size_t>(width) * 4);
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    self->m_picture = picture;
  }

  VTDecompressionSessionRef m_session = nullptr;
  CMVideoFormatDescriptionRef m_format = nullptr;
  QByteArray m_sps;
  QByteArray m_pps;
  QImage m_picture;
  bool m_failed = false;
};

} // namespace

std::unique_ptr<VideoEncoder> createH264Encoder()
{
  return std::make_unique<VTEncoder>();
}

std::unique_ptr<VideoDecoder> createH264Decoder()
{
  return std::make_unique<VTDecoder>();
}

} // namespace deskflow::remote
