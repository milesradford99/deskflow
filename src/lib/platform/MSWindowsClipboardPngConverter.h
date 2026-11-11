/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Pass PNG images to and from the registered "PNG" clipboard format
/*!
Browsers, Office and most modern apps exchange images in this format,
and it is the only common one that keeps transparency.
*/
class MSWindowsClipboardPngConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardPngConverter();
  ~MSWindowsClipboardPngConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;

private:
  UINT m_format;
};

//! Convert between PNG and CF_DIB
/*!
Covers apps that only understand classic bitmaps: screenshots taken with
Print Screen, Paint, and older software.
*/
class MSWindowsClipboardDibPngConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardDibPngConverter() = default;
  ~MSWindowsClipboardDibPngConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;
};
