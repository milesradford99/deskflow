/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFilesConverter.h"

#include "deskflow/ClipboardFileSync.h"

#include <QDir>
#include <QString>

#include <cstring>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>

using deskflow::ClipboardFileSync;

//
// MSWindowsClipboardFilesConverter
//

IClipboard::Format MSWindowsClipboardFilesConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardFilesConverter::getWin32Format() const
{
  return CF_HDROP;
}

HANDLE MSWindowsClipboardFilesConverter::fromIClipboard(const std::string &data) const
{
  // a DROPFILES header, then each path as a null terminated wide string, then one more null
  std::wstring list;
  const auto paths = ClipboardFileSync::splitPaths(data);
  for (const auto &path : paths) {
    list += QDir::toNativeSeparators(path).toStdWString();
    list += L'\0';
  }
  if (list.empty()) {
    return nullptr;
  }
  list += L'\0';

  const auto bytes = list.size() * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
  if (handle == nullptr) {
    return nullptr;
  }

  auto *drop = static_cast<DROPFILES *>(GlobalLock(handle));
  if (drop == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  drop->pFiles = sizeof(DROPFILES);
  drop->fWide = TRUE;
  std::memcpy(reinterpret_cast<char *>(drop) + sizeof(DROPFILES), list.data(), bytes);
  GlobalUnlock(handle);
  return handle;
}

std::string MSWindowsClipboardFilesConverter::toIClipboard(HANDLE data) const
{
  const auto drop = static_cast<HDROP>(data);
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

  QStringList paths;
  for (UINT i = 0; i < count; ++i) {
    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
    if (length == 0) {
      continue;
    }
    std::vector<wchar_t> buffer(length + 1);
    DragQueryFileW(drop, i, buffer.data(), length + 1);
    paths.append(QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length))));
  }
  return ClipboardFileSync::joinPaths(paths);
}

//
// MSWindowsClipboardDropEffectConverter
//

MSWindowsClipboardDropEffectConverter::MSWindowsClipboardDropEffectConverter()
    : m_format(RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT))
{
  // do nothing
}

IClipboard::Format MSWindowsClipboardDropEffectConverter::getFormat() const
{
  return IClipboard::Format::Files;
}

UINT MSWindowsClipboardDropEffectConverter::getWin32Format() const
{
  return m_format;
}

HANDLE MSWindowsClipboardDropEffectConverter::fromIClipboard(const std::string &) const
{
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
  if (handle == nullptr) {
    return nullptr;
  }

  auto *effect = static_cast<DWORD *>(GlobalLock(handle));
  if (effect == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  *effect = DROPEFFECT_COPY;
  GlobalUnlock(handle);
  return handle;
}

std::string MSWindowsClipboardDropEffectConverter::toIClipboard(HANDLE) const
{
  // the effect alone says nothing about which files; CF_HDROP is what gets read
  return {};
}
