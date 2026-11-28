/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

namespace deskflow::remote {

//! Takes pictures of the screen for the remote view
/*!
Everything is called from one capture thread, never the core's event thread.
ScreenCaptureKit on macOS, GDI on Windows, the X server elsewhere.
*/
class ScreenCapturer
{
public:
  enum class Status
  {
    Ok,
    Unsupported, //!< this platform or session can't be captured (e.g. Wayland)
    Denied,      //!< the user hasn't allowed screen recording
    Failed
  };

  virtual ~ScreenCapturer() = default;

  //! The displays, in the coordinates the computer uses for absolute mouse moves; the main one first
  virtual std::vector<QRect> displays() = 0;

  //! Start capturing display \c index; pictures may come scaled down to fit \c maxSize
  virtual Status start(int index, const QSize &maxSize, int fps) = 0;

  //! The latest picture, waiting at most \c timeoutMs for a new one. Null if nothing new arrived.
  virtual QImage grab(int timeoutMs) = 0;

  virtual void stop() = 0;

  //! Why start() failed, for the log
  virtual QString detail() const
  {
    return {};
  }
};

//! The capturer for this platform; \c x11Display is the X display name ("" for $DISPLAY)
std::unique_ptr<ScreenCapturer> createScreenCapturer(const QString &x11Display = {});

} // namespace deskflow::remote
