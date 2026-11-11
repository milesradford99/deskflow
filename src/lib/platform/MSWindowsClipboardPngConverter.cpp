/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardPngConverter.h"

#include "deskflow/ClipboardImage.h"

#include <cstring>

namespace {

HANDLE toGlobal(const std::string &data)
{
  if (data.empty()) {
    return nullptr;
  }

  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, data.size());
  if (handle == nullptr) {
    return nullptr;
  }

  auto *dst = static_cast<char *>(GlobalLock(handle));
  if (dst == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  std::memcpy(dst, data.data(), data.size());
  GlobalUnlock(handle);
  return handle;
}

std::string fromGlobal(HANDLE handle)
{
  std::string data;
  const auto *src = static_cast<const char *>(GlobalLock(handle));
  if (src == nullptr) {
    return data;
  }

  data.assign(src, GlobalSize(handle));
  GlobalUnlock(handle);
  return data;
}

} // namespace

//
// MSWindowsClipboardPngConverter
//

MSWindowsClipboardPngConverter::MSWindowsClipboardPngConverter() : m_format(RegisterClipboardFormat(TEXT("PNG")))
{
  // do nothing
}

IClipboard::Format MSWindowsClipboardPngConverter::getFormat() const
{
  return IClipboard::Format::Png;
}

UINT MSWindowsClipboardPngConverter::getWin32Format() const
{
  return m_format;
}

HANDLE MSWindowsClipboardPngConverter::fromIClipboard(const std::string &data) const
{
  return toGlobal(data);
}

std::string MSWindowsClipboardPngConverter::toIClipboard(HANDLE data) const
{
  // GlobalSize() may round up, so cut the padding off after the last chunk
  return deskflow::clipboardImage::trimPng(fromGlobal(data));
}

//
// MSWindowsClipboardDibPngConverter
//

IClipboard::Format MSWindowsClipboardDibPngConverter::getFormat() const
{
  return IClipboard::Format::Png;
}

UINT MSWindowsClipboardDibPngConverter::getWin32Format() const
{
  return CF_DIB;
}

HANDLE MSWindowsClipboardDibPngConverter::fromIClipboard(const std::string &data) const
{
  return toGlobal(deskflow::clipboardImage::pngToDib(data));
}

std::string MSWindowsClipboardDibPngConverter::toIClipboard(HANDLE data) const
{
  return deskflow::clipboardImage::dibToPng(fromGlobal(data));
}
