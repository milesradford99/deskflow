/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Remote view capture on Windows with GDI, cursor drawn in.

#include "remoteview/ScreenCapturer.h"

#include <QElapsedTimer>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>

namespace deskflow::remote {

namespace {

BOOL CALLBACK addMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
  auto *rects = reinterpret_cast<std::vector<QRect> *>(data);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    const RECT &r = info.rcMonitor;
    const QRect rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    if (info.dwFlags & MONITORINFOF_PRIMARY)
      rects->insert(rects->begin(), rect);
    else
      rects->push_back(rect);
  }
  return TRUE;
}

class GdiCapturer : public ScreenCapturer
{
public:
  ~GdiCapturer() override
  {
    stop();
  }

  std::vector<QRect> displays() override
  {
    std::vector<QRect> rects;
    EnumDisplayMonitors(nullptr, nullptr, &addMonitor, reinterpret_cast<LPARAM>(&rects));
    return rects;
  }

  Status start(int index, const QSize &, int fps) override
  {
    stop();
    followInputDesktop();
    const auto rects = displays();
    if (rects.empty()) {
      m_detail = QStringLiteral("no displays");
      return Status::Failed;
    }
    m_rect = rects[std::clamp<size_t>(index, 0, rects.size() - 1)];
    m_interval = 1000 / std::max(fps, 1);
    m_timer.start();

    m_memory = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = m_rect.width();
    info.bmiHeader.biHeight = -m_rect.height(); // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    m_bitmap = CreateDIBSection(m_memory, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
    if (m_memory == nullptr || m_bitmap == nullptr) {
      m_detail = QStringLiteral("can't create a capture bitmap");
      stop();
      return Status::Failed;
    }
    m_previousBitmap = SelectObject(m_memory, m_bitmap);
    return Status::Ok;
  }

  QImage grab(int timeoutMs) override
  {
    if (m_memory == nullptr)
      return {};
    const auto wait = m_interval - m_timer.elapsed();
    if (wait > 0)
      QThread::msleep(static_cast<unsigned long>(std::min<qint64>(wait, timeoutMs)));
    if (m_timer.elapsed() < m_interval)
      return {};
    m_timer.restart();

    HDC screen = GetDC(nullptr);
    BOOL copied =
        BitBlt(m_memory, 0, 0, m_rect.width(), m_rect.height(), screen, m_rect.x(), m_rect.y(), SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);
    if (!copied) {
      // the secure desktop (UAC, lock screen) or a desktop switch: follow the input desktop and try again
      followInputDesktop();
      return {};
    }
    drawCursor();
    GdiFlush();

    QImage picture(m_rect.width(), m_rect.height(), QImage::Format_RGB32);
    const auto rowBytes = static_cast<size_t>(m_rect.width()) * 4;
    for (int y = 0; y < m_rect.height(); ++y) {
      auto *line = reinterpret_cast<QRgb *>(picture.scanLine(y));
      std::memcpy(line, static_cast<const uchar *>(m_bits) + y * rowBytes, rowBytes);
      for (int x = 0; x < m_rect.width(); ++x)
        line[x] |= 0xff000000;
    }
    return picture;
  }

  void stop() override
  {
    if (m_memory != nullptr && m_previousBitmap != nullptr)
      SelectObject(m_memory, m_previousBitmap);
    if (m_bitmap != nullptr)
      DeleteObject(m_bitmap);
    if (m_memory != nullptr)
      DeleteDC(m_memory);
    m_bitmap = nullptr;
    m_memory = nullptr;
    m_previousBitmap = nullptr;
    m_bits = nullptr;
  }

  QString detail() const override
  {
    return m_detail;
  }

private:
  static void followInputDesktop()
  {
    if (HDESK desktop = OpenInputDesktop(0, FALSE, GENERIC_ALL); desktop != nullptr) {
      SetThreadDesktop(desktop);
      CloseDesktop(desktop);
    }
  }

  void drawCursor()
  {
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING))
      return;
    ICONINFO icon{};
    if (!GetIconInfo(cursor.hCursor, &icon))
      return;
    const int x = cursor.ptScreenPos.x - static_cast<int>(icon.xHotspot) - m_rect.x();
    const int y = cursor.ptScreenPos.y - static_cast<int>(icon.yHotspot) - m_rect.y();
    DrawIconEx(m_memory, x, y, cursor.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    if (icon.hbmMask != nullptr)
      DeleteObject(icon.hbmMask);
    if (icon.hbmColor != nullptr)
      DeleteObject(icon.hbmColor);
  }

  QRect m_rect;
  HDC m_memory = nullptr;
  HBITMAP m_bitmap = nullptr;
  HGDIOBJ m_previousBitmap = nullptr;
  void *m_bits = nullptr;
  qint64 m_interval = 33;
  QElapsedTimer m_timer;
  QString m_detail;
};

} // namespace

std::unique_ptr<ScreenCapturer> createScreenCapturer(const QString &)
{
  return std::make_unique<GdiCapturer>();
}

} // namespace deskflow::remote
