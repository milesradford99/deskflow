/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/RemoteViewSender.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "remoteview/RemoteFrame.h"
#include "remoteview/StreamEncoder.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace deskflow::client {

using namespace deskflow::remote;

namespace {

constexpr int kGrabTimeoutMs = 100;
constexpr int kRetryMs = 2000;

QString stateName(ScreenCapturer::Status status)
{
  switch (status) {
    using enum ScreenCapturer::Status;
  case Ok:
    return QStringLiteral("streaming");
  case Unsupported:
    return QStringLiteral("unsupported");
  case Denied:
    return QStringLiteral("denied");
  default:
    return QStringLiteral("failed");
  }
}

} // namespace

RemoteViewSender::RemoteViewSender(IEventQueue *events, CapturerFactory factory, SendPiece sendPiece, SendInfo sendInfo)
    : m_events(events),
      m_factory(std::move(factory)),
      m_sendPiece(std::move(sendPiece)),
      m_sendInfo(std::move(sendInfo))
{
  m_events->addHandler(EventTypes::RemoteViewOutput, this, [this](const auto &) { flush(); });
}

RemoteViewSender::~RemoteViewSender()
{
  stop();
  m_events->removeHandler(EventTypes::RemoteViewOutput, this);
}

void RemoteViewSender::applySettings(const std::string &json)
{
  const auto object = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
  if (!object.value("on").toBool()) {
    if (isStreaming())
      LOG_INFO("remote view: stopped");
    stop();
    return;
  }

  Settings settings;
  settings.display = std::max(0, object.value("display").toInt());
  settings.quality = std::clamp(object.value("quality").toInt(1), 0, 2);
  settings.fps = std::clamp(object.value("fps").toInt(30), 1, 60);
  settings.h264 = object.value("codecs").toArray().contains(QStringLiteral("h264"));
  {
    std::scoped_lock lock{m_mutex};
    if (!(settings == m_settings)) {
      m_settings = settings;
      ++m_settingsVersion;
    }
    if (object.value("key").toBool())
      m_wantKey = true;
  }
  m_wake.notify_all();
  if (!isStreaming())
    start();
}

void RemoteViewSender::acknowledge(uint32_t sequence)
{
  {
    std::scoped_lock lock{m_mutex};
    if (sequence > m_lastAcked && sequence <= m_lastSent)
      m_lastAcked = sequence;
  }
  m_wake.notify_all();
}

void RemoteViewSender::start()
{
  LOG_INFO("remote view: a server is watching this computer's screen");
  {
    std::scoped_lock lock{m_mutex};
    m_stop = false;
    m_lastAcked = m_lastSent; // a fresh stream starts with nothing in flight
    m_wantKey = true;
  }
  m_thread = std::thread([this] { run(); });
}

void RemoteViewSender::stop()
{
  if (!m_thread.joinable())
    return;
  {
    std::scoped_lock lock{m_mutex};
    m_stop = true;
  }
  m_wake.notify_all();
  m_thread.join();
  std::scoped_lock lock{m_mutex};
  m_outbox.clear();
}

void RemoteViewSender::post(Output output)
{
  bool postEvent = false;
  {
    std::scoped_lock lock{m_mutex};
    m_outbox.push_back(std::move(output));
    postEvent = !m_flushPosted;
    m_flushPosted = true;
  }
  if (postEvent)
    m_events->addEvent(Event(EventTypes::RemoteViewOutput, this));
}

void RemoteViewSender::flush()
{
  std::deque<Output> outbox;
  {
    std::scoped_lock lock{m_mutex};
    outbox.swap(m_outbox);
    m_flushPosted = false;
  }
  for (const auto &output : outbox) {
    if (output.info)
      m_sendInfo(output.data);
    else
      m_sendPiece(output.data);
  }
}

void RemoteViewSender::postInfo(
    const QString &state, const std::vector<QRect> &displays, const QString &codec, const QString &detail
)
{
  QJsonObject info;
  info["state"] = state;
  QJsonArray list;
  for (const auto &rect : displays)
    list.append(QJsonObject{{"x", rect.x()}, {"y", rect.y()}, {"w", rect.width()}, {"h", rect.height()}});
  info["displays"] = list;
  info["codec"] = codec;
  info["detail"] = detail;
  post({true, QJsonDocument(info).toJson(QJsonDocument::Compact).toStdString()});
}

void RemoteViewSender::run()
{
  auto capturer = m_factory ? m_factory() : nullptr;
  if (!capturer) {
    postInfo(QStringLiteral("unsupported"), {}, {}, QStringLiteral("no screen capture on this platform"));
    std::unique_lock lock{m_mutex};
    m_wake.wait(lock, [this] { return m_stop; });
    return;
  }

  StreamEncoder encoder;
  uint64_t version = 0;
  bool capturing = false;
  Settings settings;
  std::vector<QRect> displays;
  QRect source;
  QImage last;
  QString lastCodec;
  uint32_t sequence = 0;
  {
    std::scoped_lock lock{m_mutex};
    sequence = m_lastSent;
  }

  while (true) {
    bool wantKey = false;
    {
      std::unique_lock lock{m_mutex};
      // wait while the server has two frames it hasn't shown yet
      m_wake.wait(lock, [this] { return m_stop || m_lastSent - m_lastAcked < kMaxUnacknowledged; });
      if (m_stop)
        break;
      if (version != m_settingsVersion || !capturing) {
        version = m_settingsVersion;
        settings = m_settings;
        capturing = false;
      }
      wantKey = m_wantKey;
      m_wantKey = false;
    }

    if (!capturing) {
      const auto quality = static_cast<Quality>(settings.quality);
      displays = capturer->displays();
      const int display = std::clamp(settings.display, 0, std::max(0, static_cast<int>(displays.size()) - 1));
      const auto status = capturer->start(display, presetMaxSize(quality), settings.fps);
      encoder.configure(settings.h264, quality, settings.fps);
      encoder.requestKeyFrame();
      lastCodec.clear();
      if (status != ScreenCapturer::Status::Ok) {
        LOG_WARN("remote view: can't capture the screen: %s", qPrintable(capturer->detail()));
        postInfo(stateName(status), displays, {}, capturer->detail());
        std::unique_lock lock{m_mutex};
        // try again in a while (the user may grant the permission), or as soon as the settings change
        m_wake.wait_for(lock, std::chrono::milliseconds(kRetryMs), [this, version] {
          return m_stop || version != m_settingsVersion;
        });
        continue;
      }
      source = displays.empty() ? QRect() : displays[static_cast<size_t>(display)];
      capturing = true;
      last = QImage();
      settings.display = display;
    }
    if (wantKey)
      encoder.requestKeyFrame();

    QImage picture = capturer->grab(kGrabTimeoutMs);
    if (picture.isNull()) {
      if (!encoder.hasIdleWork() || last.isNull())
        continue;
      picture = last; // nothing new on screen: use the time to sharpen tiles
    } else {
      const QSize size = fitSize(picture.size(), static_cast<Quality>(settings.quality));
      if (size != picture.size())
        picture = picture.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      if (picture.format() != QImage::Format_RGB32)
        picture = picture.convertToFormat(QImage::Format_RGB32);
      last = picture;
    }

    bool key = false;
    auto tiles = encoder.encode(picture, key);
    if (encoder.codecName() != lastCodec) {
      lastCodec = encoder.codecName();
      LOG_INFO(
          "remote view: streaming display %d at %dx%d with %s", settings.display, picture.width(), picture.height(),
          qPrintable(lastCodec)
      );
      postInfo(QStringLiteral("streaming"), displays, lastCodec, {});
    }
    if (tiles.empty())
      continue;

    FrameHeader header;
    header.flags = key ? FrameHeader::kKeyFrame : 0;
    header.sequence = ++sequence;
    header.source = source;
    header.size = picture.size();
    header.display = static_cast<uint8_t>(settings.display);
    header.displayCount = static_cast<uint8_t>(std::clamp<size_t>(displays.size(), 1, 255));
    for (auto &piece : splitIntoPieces(header, std::move(tiles)))
      post({false, piece.toStdString()});
    std::scoped_lock lock{m_mutex};
    m_lastSent = sequence;
  }
  capturer->stop();
}

} // namespace deskflow::client
