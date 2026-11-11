/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

class ClipboardImageTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void isPng_detectsSignature();
  void trimPng_dropsTrailingBytes();
  void trimPng_rejectsTruncated();
  void pngToDib_writesBottomUpBgr();
  void pngToDib_flattensTransparencyOntoWhite();
  void dibToPng_roundTripsPixels();
  void dibToPng_rejectsGarbage();
  void marshall_sendsLoneBitmapAsPng();
  void marshall_prefersPngOverBitmap();
  void copy_skipsBitmapWhenPngPresent();

private:
  Log m_log;
};
