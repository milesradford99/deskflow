/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboard.h"

//! Convert between PNG and a native pasteboard image type
/*!
macOS apps disagree about which image type they read and write: screenshots
are PNG, Safari and Preview copy TIFF, and some apps only paste one of the two.
One instance exists per pasteboard type; all of them map to \c Format::Png.
*/
class OSXClipboardImageConverter : public IOSXClipboardConverter
{
public:
  enum class Mode
  {
    ReadWrite, //!< Offer this type when writing an image to the pasteboard
    ReadOnly   //!< Only accept this type when reading the pasteboard
  };

  OSXClipboardImageConverter(CFStringRef uti, Mode mode);
  ~OSXClipboardImageConverter() override = default;

  IClipboard::Format getFormat() const override;
  CFStringRef getOSXFormat() const override;

  std::string fromIClipboard(const std::string &png) const override;
  std::string toIClipboard(const std::string &native) const override;

  //! Re-encode \c image (any type ImageIO can read) as \c uti; empty on failure.
  static std::string transcode(const std::string &image, CFStringRef uti);

private:
  bool isPng() const;

  CFStringRef m_uti;
  Mode m_mode;
};
