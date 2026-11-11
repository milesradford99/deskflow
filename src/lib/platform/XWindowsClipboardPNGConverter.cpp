/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/XWindowsClipboardPNGConverter.h"

#include "deskflow/ClipboardImage.h"

XWindowsClipboardPNGConverter::XWindowsClipboardPNGConverter(Display *display)
    : m_atom(XInternAtom(display, "image/png", False))
{
  // do nothing
}

IClipboard::Format XWindowsClipboardPNGConverter::getFormat() const
{
  return IClipboard::Format::Png;
}

Atom XWindowsClipboardPNGConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardPNGConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardPNGConverter::fromIClipboard(const std::string &data) const
{
  return data;
}

std::string XWindowsClipboardPNGConverter::toIClipboard(const std::string &data) const
{
  return deskflow::clipboardImage::isPng(data) ? data : std::string();
}
