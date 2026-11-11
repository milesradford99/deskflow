/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardImageConverter.h"

#include "base/Log.h"
#include "deskflow/ClipboardImage.h"

#include <ImageIO/ImageIO.h>

namespace {
const CFStringRef kPngUti = CFSTR("public.png");
}

OSXClipboardImageConverter::OSXClipboardImageConverter(CFStringRef uti, Mode mode) : m_uti(uti), m_mode(mode)
{
  // do nothing
}

IClipboard::Format OSXClipboardImageConverter::getFormat() const
{
  return IClipboard::Format::Png;
}

CFStringRef OSXClipboardImageConverter::getOSXFormat() const
{
  return m_uti;
}

bool OSXClipboardImageConverter::isPng() const
{
  return CFStringCompare(m_uti, kPngUti, 0) == kCFCompareEqualTo;
}

std::string OSXClipboardImageConverter::fromIClipboard(const std::string &png) const
{
  if (m_mode == Mode::ReadOnly) {
    return {};
  }
  return isPng() ? png : transcode(png, m_uti);
}

std::string OSXClipboardImageConverter::toIClipboard(const std::string &native) const
{
  if (isPng() && deskflow::clipboardImage::isPng(native)) {
    return native;
  }
  return transcode(native, kPngUti);
}

std::string OSXClipboardImageConverter::transcode(const std::string &image, CFStringRef uti)
{
  std::string result;
  if (image.empty()) {
    return result;
  }

  CFDataRef input = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(image.data()), image.size());
  if (input == nullptr) {
    return result;
  }

  CGImageSourceRef source = CGImageSourceCreateWithData(input, nullptr);
  CFMutableDataRef output = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGImageDestinationRef destination = nullptr;

  if (source != nullptr && output != nullptr && CGImageSourceGetCount(source) > 0) {
    destination = CGImageDestinationCreateWithData(output, uti, 1, nullptr);
  }

  if (destination != nullptr) {
    // copying from the source (rather than a decoded CGImage) keeps the DPI,
    // so retina screenshots paste at their original point size
    CGImageDestinationAddImageFromSource(destination, source, 0, nullptr);
    if (CGImageDestinationFinalize(destination)) {
      result.assign(reinterpret_cast<const char *>(CFDataGetBytePtr(output)), CFDataGetLength(output));
    } else {
      LOG_DEBUG("failed to encode clipboard image");
    }
  } else {
    LOG_DEBUG("failed to decode clipboard image, size: %zu bytes", image.size());
  }

  if (destination != nullptr)
    CFRelease(destination);
  if (output != nullptr)
    CFRelease(output);
  if (source != nullptr)
    CFRelease(source);
  CFRelease(input);
  return result;
}
