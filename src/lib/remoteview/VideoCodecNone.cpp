/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Builds without an H.264 codec; the remote view falls back to JPEG/PNG tiles.

#include "remoteview/VideoCodec.h"

namespace deskflow::remote {

std::unique_ptr<VideoEncoder> createH264Encoder()
{
  return nullptr;
}

std::unique_ptr<VideoDecoder> createH264Decoder()
{
  return nullptr;
}

} // namespace deskflow::remote
