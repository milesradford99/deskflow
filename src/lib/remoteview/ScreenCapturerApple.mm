/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Remote view capture on macOS with ScreenCaptureKit (12.3+). The system scales the
// picture on the GPU and only delivers frames when something on screen changed.
// Needs the Screen Recording permission, which macOS grants to the Deskflow app
// (the core inherits it). This file is built without ARC.

#include "remoteview/ScreenCapturer.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace deskflow::remote {
class SCKCapturer;
}

//! Receives frames on a private queue and hands the latest to the capturer
API_AVAILABLE(macos(12.3))
@interface DFScreenStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::mutex *mutex;
  std::condition_variable *ready;
  QImage *latest;
  bool *fresh;
  bool *stopped;
}
@end

@implementation DFScreenStreamOutput

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type
{
  if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))
    return;

  // idle and blank frames carry no picture
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (attachments != nullptr && CFArrayGetCount(attachments) > 0) {
    NSDictionary *info = (NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = info[SCStreamFrameInfoStatus];
    if (status != nil && status.integerValue != SCFrameStatusComplete)
      return;
  }

  CVImageBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
  if (buffer == nullptr)
    return;
  CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
  const auto width = static_cast<int>(CVPixelBufferGetWidth(buffer));
  const auto height = static_cast<int>(CVPixelBufferGetHeight(buffer));
  const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
  const auto *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
  QImage picture(width, height, QImage::Format_RGB32);
  for (int row = 0; row < height; ++row)
    std::memcpy(picture.scanLine(row), base + row * stride, static_cast<size_t>(width) * 4);
  CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

  {
    std::scoped_lock lock{*mutex};
    *latest = picture;
    *fresh = true;
  }
  ready->notify_all();
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
  {
    std::scoped_lock lock{*mutex};
    *stopped = true;
  }
  ready->notify_all();
}

@end

namespace deskflow::remote {

namespace {

//! Displays in the order shown to the user: the main one first
std::vector<CGDirectDisplayID> displayIds()
{
  CGDirectDisplayID ids[16];
  uint32_t count = 0;
  if (CGGetActiveDisplayList(16, ids, &count) != kCGErrorSuccess)
    return {};
  std::vector<CGDirectDisplayID> list(ids, ids + count);
  const auto main = std::find(list.begin(), list.end(), CGMainDisplayID());
  if (main != list.end())
    std::rotate(list.begin(), main, main + 1);
  return list;
}

} // namespace

class SCKCapturer : public ScreenCapturer
{
public:
  ~SCKCapturer() override
  {
    stop();
  }

  std::vector<QRect> displays() override
  {
    std::vector<QRect> rects;
    for (const auto id : displayIds()) {
      const CGRect bounds = CGDisplayBounds(id);
      rects.emplace_back(
          static_cast<int>(bounds.origin.x), static_cast<int>(bounds.origin.y), static_cast<int>(bounds.size.width),
          static_cast<int>(bounds.size.height)
      );
    }
    return rects;
  }

  Status start(int index, const QSize &maxSize, int fps) override
  {
    stop();
    if (@available(macOS 12.3, *)) {
      return startStream(index, maxSize, fps);
    }
    m_detail = QStringLiteral("screen capture needs macOS 12.3 or later");
    return Status::Unsupported;
  }

  QImage grab(int timeoutMs) override
  {
    std::unique_lock lock{m_mutex};
    m_ready.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_fresh || m_stopped; });
    if (!m_fresh)
      return {};
    m_fresh = false;
    return m_latest;
  }

  void stop() override
  {
    if (@available(macOS 12.3, *)) {
      if (m_stream != nil) {
        SCStream *stream = (SCStream *)m_stream;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [stream stopCaptureWithCompletionHandler:^(NSError *) {
          dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
        dispatch_release(done);
        [stream release];
        m_stream = nil;
      }
      if (m_output != nil) {
        [(DFScreenStreamOutput *)m_output release];
        m_output = nil;
      }
      if (m_queue != nullptr) {
        dispatch_release(m_queue);
        m_queue = nullptr;
      }
    }
    std::scoped_lock lock{m_mutex};
    m_latest = QImage();
    m_fresh = false;
    m_stopped = false;
  }

  QString detail() const override
  {
    return m_detail;
  }

private:
  API_AVAILABLE(macos(12.3))
  Status startStream(int index, const QSize &maxSize, int fps)
  {
    if (!CGPreflightScreenCaptureAccess()) {
      CGRequestScreenCaptureAccess(); // shows the system prompt the first time
      m_detail = QStringLiteral("Screen Recording permission not granted");
      return Status::Denied;
    }

    const auto ids = displayIds();
    if (ids.empty()) {
      m_detail = QStringLiteral("no displays");
      return Status::Failed;
    }
    const CGDirectDisplayID displayId = ids[std::clamp<size_t>(index, 0, ids.size() - 1)];

    __block SCShareableContent *content = nil;
    __block NSError *contentError = nil;
    dispatch_semaphore_t got = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent *result, NSError *error) {
                                                   content = [result retain];
                                                   contentError = [error retain];
                                                   dispatch_semaphore_signal(got);
                                                 }];
    const long timedOut = dispatch_semaphore_wait(got, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    dispatch_release(got);
    if (timedOut != 0 || content == nil) {
      m_detail = contentError != nil ? QString::fromNSString(contentError.localizedDescription)
                                     : QStringLiteral("no shareable content");
      const bool denied = contentError != nil && contentError.code == SCStreamErrorUserDeclined;
      [contentError release];
      [content release];
      return denied ? Status::Denied : Status::Failed;
    }
    [contentError release];

    SCDisplay *display = nil;
    for (SCDisplay *candidate in content.displays) {
      if (candidate.displayID == displayId)
        display = candidate;
    }
    if (display == nil) {
      [content release];
      m_detail = QStringLiteral("display not found");
      return Status::Failed;
    }

    // capture at the display's pixel size, scaled down by the system to fit
    QSize pixels(static_cast<int>(CGDisplayPixelsWide(displayId)), static_cast<int>(CGDisplayPixelsHigh(displayId)));
    if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayId); mode != nullptr) {
      pixels = QSize(
          static_cast<int>(CGDisplayModeGetPixelWidth(mode)), static_cast<int>(CGDisplayModeGetPixelHeight(mode))
      );
      CGDisplayModeRelease(mode);
    }
    QSize size = pixels.scaled(maxSize.boundedTo(pixels), Qt::KeepAspectRatio);
    size = QSize(std::max(2, size.width() & ~1), std::max(2, size.height() & ~1));

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    [content release];

    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.width = static_cast<size_t>(size.width());
    config.height = static_cast<size_t>(size.height());
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.minimumFrameInterval = CMTimeMake(1, std::max(fps, 1));
    config.showsCursor = YES;
    config.queueDepth = 4;

    DFScreenStreamOutput *output = [[DFScreenStreamOutput alloc] init];
    output->mutex = &m_mutex;
    output->ready = &m_ready;
    output->latest = &m_latest;
    output->fresh = &m_fresh;
    output->stopped = &m_stopped;

    SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output];
    [filter release];
    [config release];

    m_queue = dispatch_queue_create("deskflow.remoteview.capture", DISPATCH_QUEUE_SERIAL);
    NSError *addError = nil;
    if (![stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:m_queue error:&addError]) {
      m_detail = QString::fromNSString(addError.localizedDescription);
      [stream release];
      [output release];
      return Status::Failed;
    }

    __block NSError *startError = nil;
    dispatch_semaphore_t started = dispatch_semaphore_create(0);
    [stream startCaptureWithCompletionHandler:^(NSError *error) {
      startError = [error retain];
      dispatch_semaphore_signal(started);
    }];
    const long startTimedOut = dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    dispatch_release(started);
    if (startTimedOut != 0 || startError != nil) {
      m_detail = startError != nil ? QString::fromNSString(startError.localizedDescription)
                                   : QStringLiteral("capture did not start");
      const bool denied = startError != nil && startError.code == SCStreamErrorUserDeclined;
      [startError release];
      [stream release];
      [output release];
      return denied ? Status::Denied : Status::Failed;
    }

    m_stream = stream;
    m_output = output;
    return Status::Ok;
  }

  std::mutex m_mutex;
  std::condition_variable m_ready;
  QImage m_latest;
  bool m_fresh = false;
  bool m_stopped = false;
  id m_stream = nil;
  id m_output = nil;
  dispatch_queue_t m_queue = nullptr;
  QString m_detail;
};

std::unique_ptr<ScreenCapturer> createScreenCapturer(const QString &)
{
  return std::make_unique<SCKCapturer>();
}

} // namespace deskflow::remote
