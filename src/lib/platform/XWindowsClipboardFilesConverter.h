/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/XWindowsClipboard.h"

//! Convert copied files to and from the URI lists X11 file managers exchange
class XWindowsClipboardFilesConverter : public IXWindowsClipboardConverter
{
public:
  enum class Flavor
  {
    UriList,    //!< "text/uri-list": KDE, Xfce and drag and drop everywhere
    GnomeCopied //!< "x-special/gnome-copied-files": GNOME Files and its relatives
  };

  XWindowsClipboardFilesConverter(Display *display, Flavor flavor);
  ~XWindowsClipboardFilesConverter() override = default;

  // IXWindowsClipboardConverter overrides
  IClipboard::Format getFormat() const override;
  Atom getAtom() const override;
  int getDataSize() const override;
  std::string fromIClipboard(const std::string &) const override;
  std::string toIClipboard(const std::string &) const override;

private:
  Flavor m_flavor;
  Atom m_atom;
};
