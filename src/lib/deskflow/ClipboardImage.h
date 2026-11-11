/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <string>

//! Clipboard image conversion
/*!
Images cross the wire as PNG (\c IClipboard::Format::Png). These helpers convert
between PNG and the legacy \c IClipboard::Format::Bitmap representation, which is
a BMP file without its 14 byte file header (a Windows DIB).
*/
namespace deskflow::clipboardImage {

//! Returns true if \c data starts with the PNG signature.
bool isPng(const std::string &data);

//! Drop any bytes after the PNG end chunk; empty if \c data isn't a well formed PNG.
std::string trimPng(const std::string &data);

//! Convert a DIB to PNG. Returns an empty string if the DIB can't be decoded.
std::string dibToPng(const std::string &dib);

//! Convert a PNG to a 32 bit bottom-up BI_RGB DIB.
/*!
Transparent pixels are flattened onto white, since consumers of plain DIBs
disagree about whether the fourth byte is alpha.
Returns an empty string if the PNG can't be decoded.
*/
std::string pngToDib(const std::string &png);

} // namespace deskflow::clipboardImage
