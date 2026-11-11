/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsClipboard.h"

//! Convert copied files to and from CF_HDROP, the format Explorer copies and pastes
class MSWindowsClipboardFilesConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardFilesConverter() = default;
  ~MSWindowsClipboardFilesConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;
};

//! Marks files put on the clipboard as copied (not cut), so pasting never deletes our cached copy
class MSWindowsClipboardDropEffectConverter : public IMSWindowsClipboardConverter
{
public:
  MSWindowsClipboardDropEffectConverter();
  ~MSWindowsClipboardDropEffectConverter() override = default;

  // IMSWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  UINT getWin32Format() const override;
  HANDLE fromIClipboard(const std::string &) const override;
  std::string toIClipboard(HANDLE) const override;

private:
  UINT m_format;
};
