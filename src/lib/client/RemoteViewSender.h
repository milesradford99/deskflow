/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "remoteview/ScreenCapturer.h"

#include <QByteArray>
#include <QJsonObject>
#include <QSize>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class IEventQueue;

namespace deskflow::client {

//! Streams this computer's screen to one server
/*!
The server asks for a stream with settings (on/off, display, quality, frame
rate, whether it can decode H.264). A capture thread takes pictures, encodes
them (H.264 if both ends can, else tiles) and hands pieces to the core thread,
which writes them to the connection. The server acknowledges each frame once its
GUI has shown it; with two frames unacknowledged the capture thread waits, so a
slow network or viewer lowers the frame rate instead of piling up data.

Public methods are for the core thread.
*/
class RemoteViewSender
{
public:
  using SendPiece = std::function<bool(const std::string &)>;
  using SendInfo = std::function<void(const std::string &)>;
  using CapturerFactory = std::function<std::unique_ptr<remote::ScreenCapturer>()>;

  RemoteViewSender(IEventQueue *events, CapturerFactory factory, SendPiece sendPiece, SendInfo sendInfo);
  ~RemoteViewSender();
  RemoteViewSender(const RemoteViewSender &) = delete;
  RemoteViewSender &operator=(const RemoteViewSender &) = delete;

  //! Settings from the server (JSON, see kMsgDRemoteSettings)
  void applySettings(const std::string &json);
  void acknowledge(uint32_t sequence);
  bool isStreaming() const
  {
    return m_thread.joinable();
  }

  static constexpr uint32_t kMaxUnacknowledged = 2;

private:
  struct Settings
  {
    int display = 0;
    int quality = 1;
    int fps = 30;
    bool h264 = false;
    bool operator==(const Settings &) const = default;
  };

  struct Output
  {
    bool info = false;
    std::string data;
  };

  void start();
  void stop();
  void run();
  void post(Output output);
  void flush();
  void postInfo(const QString &state, const std::vector<QRect> &displays, const QString &codec, const QString &detail);

  IEventQueue *m_events;
  CapturerFactory m_factory;
  SendPiece m_sendPiece;
  SendInfo m_sendInfo;

  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_wake;
  bool m_stop = false;
  Settings m_settings;
  uint64_t m_settingsVersion = 0;
  bool m_wantKey = false;
  uint32_t m_lastSent = 0;
  uint32_t m_lastAcked = 0;
  std::deque<Output> m_outbox;
  bool m_flushPosted = false;
};

} // namespace deskflow::client
