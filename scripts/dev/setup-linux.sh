#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Prepare a Linux box (Ubuntu/Debian, as root) to build and test Deskflow:
# - apt: compilers, X11 development headers, Xvfb and the tools the dev scripts use
# - Qt 6 from conda-forge (distribution Qt is often older than the 6.7 we need),
#   fetched with micromamba, itself taken from conda-forge
# - clang-format 20.1.0, the version CI's lint job uses
# - a configured ./build-dev (X11 only)
#
# Everything outside apt goes under $DESKFLOW_DEV_PREFIX (default ~/.cache/deskflow-dev).
# Safe to run again; finished steps are skipped.
set -euo pipefail

PREFIX=${DESKFLOW_DEV_PREFIX:-$HOME/.cache/deskflow-dev}
BUILD=${DESKFLOW_BUILD_DIR:-$(pwd)/build-dev}
mkdir -p "$PREFIX"

echo "== apt packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null || true
apt-get install -y -qq cmake ninja-build build-essential pkg-config \
  libx11-dev libxtst-dev libxi-dev libxkbfile-dev libxinerama-dev libxrandr-dev \
  libxkbcommon-dev libxkbcommon-x11-dev libssl-dev libgl-dev \
  xvfb xdotool xclip xinput imagemagick python3-venv >/dev/null

echo "== micromamba"
MM="$PREFIX/micromamba/bin/micromamba"
if [ ! -x "$MM" ]; then
  # the package itself, straight from conda-forge (micro.mamba.pm is often unreachable)
  version=$(curl -fsSL https://conda.anaconda.org/conda-forge/linux-64/current_repodata.json |
    python3 -c 'import json,sys; d=json.load(sys.stdin); p={**d.get("packages",{}),**d.get("packages.conda",{})}; print(max((v["version"],k) for k,v in p.items() if v["name"]=="micromamba" and k.endswith(".tar.bz2"))[1])')
  mkdir -p "$PREFIX/micromamba"
  curl -fsSL "https://conda.anaconda.org/conda-forge/linux-64/$version" | tar -xj -C "$PREFIX/micromamba"
fi

echo "== Qt from conda-forge"
QT="$PREFIX/qt"
if [ ! -d "$QT/lib/cmake/Qt6" ]; then
  # libxml2-devel: conda's xkbcommon pkg-config files need it
  MAMBA_ROOT_PREFIX="$PREFIX/mamba" "$MM" create -y -q -p "$QT" -c conda-forge \
    "qt6-main>=6.8" openssl libxml2-devel >/dev/null
fi

echo "== clang-format 20.1.0"
if [ ! -x "$PREFIX/venv/bin/clang-format" ]; then
  python3 -m venv "$PREFIX/venv"
  PIP_DISABLE_PIP_VERSION_CHECK=1 "$PREFIX/venv/bin/pip" install -q clang-format==20.1.0
fi

echo "== configure $BUILD"
cmake -G Ninja -B "$BUILD" -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_LIBEI_SUPPORT=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=ON >/dev/null

cat <<DONE

Ready. Next:
  ninja -C $BUILD
  export PATH="$PREFIX/venv/bin:\$PATH"   # clang-format 20.1.0
DONE
