/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Remote view capture from an X server: XShm when the server offers it, else XGetImage.

#include "remoteview/ScreenCapturer.h"

#include "common/PlatformInfo.h"

#include <QElapsedTimer>
#include <QThread>

// Xlib after Qt: it defines Status, Bool and None
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#if HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#undef Status // Xlib's macro would rewrite ScreenCapturer::Status

#include <algorithm>
#include <bit>
#include <cstring>

namespace deskflow::remote {

namespace {

class X11Capturer : public ScreenCapturer
{
public:
  explicit X11Capturer(const QString &display) : m_displayName(display.toLocal8Bit())
  {
  }

  ~X11Capturer() override
  {
    stop();
    if (m_display != nullptr)
      XCloseDisplay(m_display);
  }

  std::vector<QRect> displays() override
  {
    if (!connect())
      return {};
    std::vector<QRect> rects;
#if HAVE_XINERAMA
    int count = 0;
    if (XineramaIsActive(m_display)) {
      if (XineramaScreenInfo *screens = XineramaQueryScreens(m_display, &count); screens != nullptr) {
        for (int i = 0; i < count; ++i)
          rects.emplace_back(screens[i].x_org, screens[i].y_org, screens[i].width, screens[i].height);
        XFree(screens);
      }
    }
#endif
    if (rects.empty()) {
      const int screen = DefaultScreen(m_display);
      rects.emplace_back(0, 0, DisplayWidth(m_display, screen), DisplayHeight(m_display, screen));
    }
    return rects;
  }

  Status start(int index, const QSize &maxSize, int fps) override
  {
    stop();
    if (deskflow::platform::isWayland()) {
      m_detail = QStringLiteral("Wayland sessions can't be captured yet");
      return Status::Unsupported;
    }
    const auto rects = displays();
    if (rects.empty()) {
      m_detail = QStringLiteral("can't open the X display");
      return Status::Failed;
    }
    m_rect = rects[std::clamp<size_t>(index, 0, rects.size() - 1)];
    m_maxSize = maxSize;
    m_interval = 1000 / std::max(fps, 1);
    m_timer.start();
    setupShm();
    return Status::Ok;
  }

  QImage grab(int timeoutMs) override
  {
    if (m_display == nullptr || m_rect.isEmpty())
      return {};
    // the X server can't say what changed without XDamage; pace the polling and let the encoder diff
    const auto wait = m_interval - m_timer.elapsed();
    if (wait > 0)
      QThread::msleep(static_cast<unsigned long>(std::min<qint64>(wait, timeoutMs)));
    if (m_timer.elapsed() < m_interval)
      return {};
    m_timer.restart();

    const Window root = DefaultRootWindow(m_display);
    XImage *image = nullptr;
    if (m_shmImage != nullptr) {
      if (XShmGetImage(m_display, root, m_shmImage, m_rect.x(), m_rect.y(), AllPlanes))
        image = m_shmImage;
    }
    if (image == nullptr) {
      image = XGetImage(
          m_display, root, m_rect.x(), m_rect.y(), static_cast<unsigned>(m_rect.width()),
          static_cast<unsigned>(m_rect.height()), AllPlanes, ZPixmap
      );
    }
    if (image == nullptr)
      return {};

    QImage picture = toQImage(image);
    if (image != m_shmImage)
      XDestroyImage(image);
    return picture;
  }

  void stop() override
  {
    if (m_shmImage != nullptr) {
      XShmDetach(m_display, &m_shm);
      XDestroyImage(m_shmImage);
      shmdt(m_shm.shmaddr);
      shmctl(m_shm.shmid, IPC_RMID, nullptr);
      m_shmImage = nullptr;
    }
  }

  QString detail() const override
  {
    return m_detail;
  }

private:
  bool connect()
  {
    if (m_display == nullptr)
      m_display = XOpenDisplay(m_displayName.isEmpty() ? nullptr : m_displayName.constData());
    return m_display != nullptr;
  }

  void setupShm()
  {
    if (!XShmQueryExtension(m_display))
      return;
    const int screen = DefaultScreen(m_display);
    m_shmImage = XShmCreateImage(
        m_display, DefaultVisual(m_display, screen), static_cast<unsigned>(DefaultDepth(m_display, screen)), ZPixmap,
        nullptr, &m_shm, static_cast<unsigned>(m_rect.width()), static_cast<unsigned>(m_rect.height())
    );
    if (m_shmImage == nullptr)
      return;
    m_shm.shmid =
        shmget(IPC_PRIVATE, static_cast<size_t>(m_shmImage->bytes_per_line) * m_shmImage->height, IPC_CREAT | 0600);
    if (m_shm.shmid < 0) {
      XDestroyImage(m_shmImage);
      m_shmImage = nullptr;
      return;
    }
    m_shm.shmaddr = m_shmImage->data = static_cast<char *>(shmat(m_shm.shmid, nullptr, 0));
    m_shm.readOnly = False;
    if (m_shm.shmaddr == reinterpret_cast<char *>(-1) || !XShmAttach(m_display, &m_shm)) {
      if (m_shm.shmaddr != reinterpret_cast<char *>(-1))
        shmdt(m_shm.shmaddr);
      shmctl(m_shm.shmid, IPC_RMID, nullptr);
      m_shmImage->data = nullptr;
      XDestroyImage(m_shmImage);
      m_shmImage = nullptr;
      return;
    }
    XSync(m_display, False);
  }

  static QImage toQImage(const XImage *image)
  {
    QImage picture(image->width, image->height, QImage::Format_RGB32);
    const bool direct = image->bits_per_pixel == 32 && image->red_mask == 0xff0000 && image->green_mask == 0xff00 &&
                        image->blue_mask == 0xff && image->byte_order == LSBFirst;
    for (int y = 0; y < image->height; ++y) {
      auto *line = reinterpret_cast<QRgb *>(picture.scanLine(y));
      if (direct) {
        std::memcpy(line, image->data + static_cast<qsizetype>(y) * image->bytes_per_line, image->width * 4);
        for (int x = 0; x < image->width; ++x)
          line[x] |= 0xff000000;
      } else {
        for (int x = 0; x < image->width; ++x) {
          const unsigned long pixel = XGetPixel(const_cast<XImage *>(image), x, y);
          line[x] = qRgb(
              static_cast<int>((pixel & image->red_mask) >> std::countr_zero(image->red_mask)) & 0xff,
              static_cast<int>((pixel & image->green_mask) >> std::countr_zero(image->green_mask)) & 0xff,
              static_cast<int>((pixel & image->blue_mask) >> std::countr_zero(image->blue_mask)) & 0xff
          );
        }
      }
    }
    return picture;
  }

  QByteArray m_displayName;
  Display *m_display = nullptr;
  XImage *m_shmImage = nullptr;
  XShmSegmentInfo m_shm{};
  QRect m_rect;
  QSize m_maxSize;
  qint64 m_interval = 33;
  QElapsedTimer m_timer;
  QString m_detail;
};

} // namespace

std::unique_ptr<ScreenCapturer> createScreenCapturer(const QString &x11Display)
{
  return std::make_unique<X11Capturer>(x11Display);
}

} // namespace deskflow::remote
