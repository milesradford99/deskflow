/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// H.264 through Media Foundation: the GPU's encoder where the driver offers one (an
// asynchronous MFT), else Microsoft's software encoder; Microsoft's decoder, which
// uses the GPU through DXVA on its own.

#include "remoteview/VideoCodec.h"

#include <QtGlobal>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace deskflow::remote {

namespace {

constexpr LONGLONG kHundredNanoseconds = 10'000'000;

//! COM and Media Foundation for the calling thread, for as long as a codec lives
class MediaFoundationScope
{
public:
  MediaFoundationScope()
  {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_uninitializeCom = SUCCEEDED(com); // S_FALSE also needs balancing; RPC_E_CHANGED_MODE does not
    m_ok = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
  }
  ~MediaFoundationScope()
  {
    if (m_ok)
      MFShutdown();
    if (m_uninitializeCom)
      CoUninitialize();
  }
  MediaFoundationScope(const MediaFoundationScope &) = delete;
  MediaFoundationScope &operator=(const MediaFoundationScope &) = delete;
  bool ok() const
  {
    return m_ok;
  }

private:
  bool m_ok = false;
  bool m_uninitializeCom = false;
};

void setCodecValue(ICodecAPI *api, const GUID &key, ULONG value)
{
  VARIANT variant;
  VariantInit(&variant);
  variant.vt = VT_UI4;
  variant.ulVal = value;
  api->SetValue(&key, &variant);
}

void setCodecFlag(ICodecAPI *api, const GUID &key, bool value)
{
  VARIANT variant;
  VariantInit(&variant);
  variant.vt = VT_BOOL;
  variant.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  api->SetValue(&key, &variant);
}

QByteArray sampleBytes(IMFSample *sample)
{
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
    return {};
  BYTE *data = nullptr;
  DWORD length = 0;
  if (FAILED(buffer->Lock(&data, nullptr, &length)))
    return {};
  QByteArray bytes(reinterpret_cast<const char *>(data), static_cast<qsizetype>(length));
  buffer->Unlock();
  return bytes;
}

ComPtr<IMFSample> makeSample(const char *data, DWORD size, LONGLONG time, LONGLONG duration)
{
  ComPtr<IMFMediaBuffer> buffer;
  ComPtr<IMFSample> sample;
  if (FAILED(MFCreateMemoryBuffer(size, &buffer)) || FAILED(MFCreateSample(&sample)))
    return nullptr;
  BYTE *target = nullptr;
  if (FAILED(buffer->Lock(&target, nullptr, nullptr)))
    return nullptr;
  if (data != nullptr)
    std::memcpy(target, data, size);
  buffer->Unlock();
  buffer->SetCurrentLength(data != nullptr ? size : 0);
  sample->AddBuffer(buffer.Get());
  sample->SetSampleTime(time);
  sample->SetSampleDuration(duration);
  return sample;
}

bool hasSps(const QByteArray &stream)
{
  for (qsizetype i = 0; i + 3 < stream.size(); ++i) {
    if (stream[i] == 0 && stream[i + 1] == 0 && stream[i + 2] == 1 && (stream[i + 3] & 0x1f) == 7)
      return true;
  }
  return false;
}

class MFEncoder : public VideoEncoder
{
public:
  bool open(const QSize &size, int fps, int bitrateKbps) override
  {
    if (!m_scope.ok())
      return false;
    m_size = size;
    m_fps = std::max(fps, 1);
    m_bitrate = static_cast<UINT32>(bitrateKbps) * 1000;

    MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};
    for (const UINT32 flags :
         {UINT32(MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER),
          UINT32(MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER)}) {
      IMFActivate **activates = nullptr;
      UINT32 count = 0;
      if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &input, &output, &activates, &count)))
        continue;
      bool configured = false;
      for (UINT32 i = 0; i < count; ++i) {
        if (!configured)
          configured = configure(activates[i]);
        activates[i]->Release();
      }
      CoTaskMemFree(activates);
      if (configured)
        return true;
    }
    return false;
  }

  QByteArray encode(const QImage &frame, bool forceKey, bool &isKey) override
  {
    isKey = false;
    if (!m_transform)
      return {};
    toNv12(frame, m_nv12);
    const LONGLONG duration = kHundredNanoseconds / m_fps;
    auto sample = makeSample(m_nv12.constData(), static_cast<DWORD>(m_nv12.size()), m_index * duration, duration);
    ++m_index;
    if (!sample)
      return {};
    if (forceKey && m_codecApi)
      setCodecValue(m_codecApi.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);

    QByteArray out;
    if (m_eventGenerator) {
      if (!encodeAsync(sample.Get(), out, isKey))
        return {};
    } else {
      HRESULT hr = m_transform->ProcessInput(m_inputId, sample.Get(), 0);
      if (hr == MF_E_NOTACCEPTING) {
        drain(out, isKey);
        hr = m_transform->ProcessInput(m_inputId, sample.Get(), 0);
      }
      if (FAILED(hr))
        return {};
      drain(out, isKey);
    }
    if (isKey && !hasSps(out))
      out.prepend(m_sequenceHeader);
    return out;
  }

  QString name() const override
  {
    return m_name;
  }

private:
  bool configure(IMFActivate *activate)
  {
    ComPtr<IMFTransform> transform;
    if (FAILED(activate->ActivateObject(IID_PPV_ARGS(&transform))))
      return false;

    ComPtr<IMFMediaEventGenerator> events;
    if (ComPtr<IMFAttributes> attributes; SUCCEEDED(transform->GetAttributes(&attributes)) && attributes) {
      UINT32 async = 0;
      if (SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &async)) && async) {
        if (FAILED(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE)) || FAILED(transform.As(&events)))
          return false;
      }
      attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    }

    DWORD inputId = 0;
    DWORD outputId = 0;
    if (const HRESULT hr = transform->GetStreamIDs(1, &inputId, 1, &outputId); hr == E_NOTIMPL) {
      inputId = 0;
      outputId = 0;
    }

    ComPtr<ICodecAPI> codecApi;
    if (SUCCEEDED(transform.As(&codecApi))) {
      setCodecFlag(codecApi.Get(), CODECAPI_AVLowLatencyMode, true);
      setCodecValue(codecApi.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
      setCodecValue(codecApi.Get(), CODECAPI_AVEncCommonMeanBitRate, m_bitrate);
      setCodecValue(codecApi.Get(), CODECAPI_AVEncMPVGOPSize, 600);
      setCodecValue(codecApi.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    }

    const auto describe = [&](IMFMediaType *type, const GUID &subtype) {
      type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
      type->SetGUID(MF_MT_SUBTYPE, subtype);
      MFSetAttributeSize(type, MF_MT_FRAME_SIZE, m_size.width(), m_size.height());
      MFSetAttributeRatio(type, MF_MT_FRAME_RATE, m_fps, 1);
      MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
      type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
      type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    };

    ComPtr<IMFMediaType> outputType;
    MFCreateMediaType(&outputType);
    describe(outputType.Get(), MFVideoFormat_H264);
    outputType->SetUINT32(MF_MT_AVG_BITRATE, m_bitrate);
    outputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    if (FAILED(transform->SetOutputType(outputId, outputType.Get(), 0)))
      return false;

    ComPtr<IMFMediaType> inputType;
    MFCreateMediaType(&inputType);
    describe(inputType.Get(), MFVideoFormat_NV12);
    if (FAILED(transform->SetInputType(inputId, inputType.Get(), 0)))
      return false;

    if (FAILED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
        FAILED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0)))
      return false;

    MFT_OUTPUT_STREAM_INFO info{};
    transform->GetOutputStreamInfo(outputId, &info);
    m_providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));
    m_outputSize = std::max<DWORD>(info.cbSize, static_cast<DWORD>(m_size.width() * m_size.height() * 2));

    if (ComPtr<IMFMediaType> current; SUCCEEDED(transform->GetOutputCurrentType(outputId, &current))) {
      UINT32 size = 0;
      if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) && size > 0) {
        m_sequenceHeader.resize(size);
        current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, reinterpret_cast<UINT8 *>(m_sequenceHeader.data()), size, nullptr);
      }
    }

    WCHAR *friendly = nullptr;
    UINT32 length = 0;
    m_name = QStringLiteral("Media Foundation");
    if (SUCCEEDED(activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly, &length))) {
      m_name = QStringLiteral("Media Foundation (%1)").arg(QString::fromWCharArray(friendly, static_cast<int>(length)));
      CoTaskMemFree(friendly);
    }

    m_transform = transform;
    m_eventGenerator = events;
    m_codecApi = codecApi;
    m_inputId = inputId;
    m_outputId = outputId;
    return true;
  }

  //! One output sample if there is one; false when the encoder needs more input
  bool takeOutput(QByteArray &out, bool &isKey)
  {
    ComPtr<IMFSample> provided;
    if (!m_providesSamples)
      provided = makeSample(nullptr, m_outputSize, 0, 0);
    MFT_OUTPUT_DATA_BUFFER buffer{m_outputId, provided.Get(), 0, nullptr};
    DWORD status = 0;
    const HRESULT hr = m_transform->ProcessOutput(0, 1, &buffer, &status);
    if (buffer.pEvents != nullptr)
      buffer.pEvents->Release();
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      ComPtr<IMFMediaType> type;
      if (SUCCEEDED(m_transform->GetOutputAvailableType(m_outputId, 0, &type)))
        m_transform->SetOutputType(m_outputId, type.Get(), 0);
      return true;
    }
    if (FAILED(hr) || buffer.pSample == nullptr)
      return false;
    out += sampleBytes(buffer.pSample);
    UINT32 clean = 0;
    if (SUCCEEDED(buffer.pSample->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean)
      isKey = true;
    if (m_providesSamples)
      buffer.pSample->Release();
    return true;
  }

  void drain(QByteArray &out, bool &isKey)
  {
    for (int i = 0; i < 8 && takeOutput(out, isKey); ++i) {
    }
  }

  //! Hardware encoders tell us when they want input and have output
  bool encodeAsync(IMFSample *sample, QByteArray &out, bool &isKey)
  {
    bool fed = false;
    const auto deadline = GetTickCount64() + 500;
    while (GetTickCount64() < deadline) {
      ComPtr<IMFMediaEvent> event;
      const HRESULT hr = m_eventGenerator->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event);
      if (hr == MF_E_NO_EVENTS_AVAILABLE) {
        if (fed && !out.isEmpty())
          return true;
        Sleep(1);
        continue;
      }
      if (FAILED(hr))
        return false;
      MediaEventType type = MEUnknown;
      event->GetType(&type);
      if (type == METransformNeedInput && !fed) {
        if (FAILED(m_transform->ProcessInput(m_inputId, sample, 0)))
          return false;
        fed = true;
      } else if (type == METransformHaveOutput) {
        takeOutput(out, isKey);
        if (fed)
          return true;
      }
    }
    return fed;
  }

  MediaFoundationScope m_scope;
  ComPtr<IMFTransform> m_transform;
  ComPtr<IMFMediaEventGenerator> m_eventGenerator;
  ComPtr<ICodecAPI> m_codecApi;
  DWORD m_inputId = 0;
  DWORD m_outputId = 0;
  bool m_providesSamples = false;
  DWORD m_outputSize = 0;
  QByteArray m_sequenceHeader;
  QByteArray m_nv12;
  QSize m_size;
  int m_fps = 30;
  UINT32 m_bitrate = 0;
  LONGLONG m_index = 0;
  QString m_name;
};

class MFDecoder : public VideoDecoder
{
public:
  bool init()
  {
    if (!m_scope.ok() ||
        FAILED(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_transform))))
      return false;
    if (ComPtr<IMFAttributes> attributes; SUCCEEDED(m_transform->GetAttributes(&attributes)) && attributes)
      attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (ComPtr<ICodecAPI> api; SUCCEEDED(m_transform.As(&api)))
      setCodecFlag(api.Get(), CODECAPI_AVLowLatencyMode, true);

    ComPtr<IMFMediaType> input;
    MFCreateMediaType(&input);
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(m_transform->SetInputType(0, input.Get(), 0)))
      return false;
    chooseOutputType();
    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
  }

  bool decode(const QByteArray &accessUnit, QImage &picture) override
  {
    auto sample = makeSample(accessUnit.constData(), static_cast<DWORD>(accessUnit.size()), m_index * 333333, 333333);
    ++m_index;
    if (!sample)
      return false;
    HRESULT hr = m_transform->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
      takeAll(picture);
      hr = m_transform->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr))
      return false;
    return takeAll(picture);
  }

  QString name() const override
  {
    return QStringLiteral("Media Foundation");
  }

private:
  void chooseOutputType()
  {
    for (DWORD i = 0;; ++i) {
      ComPtr<IMFMediaType> type;
      if (FAILED(m_transform->GetOutputAvailableType(0, i, &type)))
        break;
      GUID subtype{};
      type->GetGUID(MF_MT_SUBTYPE, &subtype);
      if (subtype != MFVideoFormat_NV12)
        continue;
      if (SUCCEEDED(m_transform->SetOutputType(0, type.Get(), 0))) {
        UINT32 width = 0;
        UINT32 height = 0;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
        m_width = static_cast<int>(width);
        m_height = static_cast<int>(height);
        m_stride = static_cast<int>(MFGetAttributeUINT32(type.Get(), MF_MT_DEFAULT_STRIDE, width));
        m_visible = QRect(0, 0, m_width, m_height);
        MFVideoArea area{};
        if (SUCCEEDED(
                type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&area), sizeof(area), nullptr)
            )) {
          m_visible = QRect(area.OffsetX.value, area.OffsetY.value, area.Area.cx, area.Area.cy) & m_visible;
        }
        MFT_OUTPUT_STREAM_INFO info{};
        m_transform->GetOutputStreamInfo(0, &info);
        m_outputSize = info.cbSize;
      }
      return;
    }
  }

  //! Collect every picture the decoder has ready; false on a decoding error
  bool takeAll(QImage &picture)
  {
    for (int i = 0; i < 8; ++i) {
      if (m_outputSize == 0)
        return true; // no output type yet: waiting for the first key frame
      auto provided = makeSample(nullptr, m_outputSize, 0, 0);
      MFT_OUTPUT_DATA_BUFFER buffer{0, provided.Get(), 0, nullptr};
      DWORD status = 0;
      const HRESULT hr = m_transform->ProcessOutput(0, 1, &buffer, &status);
      if (buffer.pEvents != nullptr)
        buffer.pEvents->Release();
      if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return true;
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        chooseOutputType();
        continue;
      }
      if (FAILED(hr))
        return false;
      const QByteArray bytes = sampleBytes(provided.Get());
      const int stride = std::abs(m_stride);
      const qsizetype need = static_cast<qsizetype>(stride) * m_height * 3 / 2;
      if (bytes.size() < need || m_width <= 0)
        return false;
      const auto *y = reinterpret_cast<const uint8_t *>(bytes.constData());
      const auto *uv = y + static_cast<qsizetype>(stride) * m_height;
      picture = fromNv12(y, stride, uv, stride, m_width, m_height).copy(m_visible);
    }
    return true;
  }

  MediaFoundationScope m_scope;
  ComPtr<IMFTransform> m_transform;
  int m_width = 0;
  int m_height = 0;
  int m_stride = 0;
  QRect m_visible;
  DWORD m_outputSize = 0;
  LONGLONG m_index = 0;
};

} // namespace

std::unique_ptr<VideoEncoder> createH264Encoder()
{
  return std::make_unique<MFEncoder>();
}

std::unique_ptr<VideoDecoder> createH264Decoder()
{
  auto decoder = std::make_unique<MFDecoder>();
  if (!decoder->init())
    return nullptr;
  return decoder;
}

} // namespace deskflow::remote
