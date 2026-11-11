/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Qt must come first: Xlib defines macros (Status, Bool, None) that break Qt headers.
#include "deskflow/ClipboardFileSync.h"

#include "platform/XWindowsClipboardFilesConverter.h"

using deskflow::ClipboardFileSync;

XWindowsClipboardFilesConverter::XWindowsClipboardFilesConverter(Display *display, Flavor flavor)
    : m_flavor(flavor),
      m_atom(XInternAtom(display, flavor == Flavor::UriList ? "text/uri-list" : "x-special/gnome-copied-files", False))
{
  // do nothing
}

IClipboard::Format XWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

Atom XWindowsClipboardFilesConverter::getAtom() const
{
  return m_atom;
}

int XWindowsClipboardFilesConverter::getDataSize() const
{
  return 8;
}

std::string XWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  const auto paths = ClipboardFileSync::splitPaths(data);
  if (m_flavor == Flavor::UriList) {
    return ClipboardFileSync::toUriList(paths, "\r\n", true);
  }
  // the first line is the operation; nothing follows the last URI
  return "copy\n" + ClipboardFileSync::toUriList(paths, "\n", false);
}

std::string XWindowsClipboardFilesConverter::toIClipboard(const std::string &data) const
{
  return ClipboardFileSync::joinPaths(ClipboardFileSync::fromUriList(data));
}
