#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Screenshot the GUI without a desktop: scripts/dev/screenshot.sh out.png [light|dark] [settings.conf]
# The optional settings file seeds a throwaway configuration (e.g. client mode, servers).
# Set SHOT_DIALOG to a window title to capture that window instead of the main one;
# SHOT_KEYS is typed into the main window first (xdotool key syntax), e.g. "alt+f".
# Needs: Xvfb, xdotool, ImageMagick, a built tree (DESKFLOW_BUILD_DIR, default ./build-dev).
set -euo pipefail

OUT=${1:?usage: screenshot.sh out.png [light|dark] [settings.conf]}
SCHEME=${2:-light}
SEED=${3:-}
BUILD=${DESKFLOW_BUILD_DIR:-$(pwd)/build-dev}
WORK=$(mktemp -d)
DISPLAY_NUM=:97
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$WORK"' EXIT

mkdir -p "$WORK/cfg/Deskflow"
{
  echo "[gui]"; echo "shownServerFirstStartMessage=true"; echo "enableUpdateCheck=false"; echo "startCoreWithGui=false"
  [ -n "$SEED" ] && cat "$SEED"
} > "$WORK/cfg/Deskflow/Deskflow.conf"

Xvfb $DISPLAY_NUM -screen 0 1400x1000x24 -nolisten tcp >/dev/null 2>&1 &
sleep 1
DISPLAY=$DISPLAY_NUM XDG_CONFIG_HOME="$WORK/cfg" XDG_STATE_HOME="$WORK/state" DESKFLOW_COLOR_SCHEME=$SCHEME \
  "$BUILD/bin/deskflow" >"$WORK/gui.log" 2>&1 &
for _ in $(seq 1 50); do
  WIN=$(DISPLAY=$DISPLAY_NUM xdotool search --onlyvisible --name '^Deskflow' 2>/dev/null | head -1 || true)
  [ -n "$WIN" ] && break
  sleep 0.2
done
[ -n "${WIN:-}" ] || { echo "GUI window did not appear"; cat "$WORK/gui.log"; exit 1; }
sleep 1.5

if [ -n "${SHOT_KEYS:-}" ]; then
  DISPLAY=$DISPLAY_NUM xdotool windowactivate --sync "$WIN" 2>/dev/null || true
  for k in $SHOT_KEYS; do DISPLAY=$DISPLAY_NUM xdotool key "$k"; sleep 0.4; done
  sleep 1
fi
if [ -n "${SHOT_DIALOG:-}" ]; then
  WIN=$(DISPLAY=$DISPLAY_NUM xdotool search --onlyvisible --name "$SHOT_DIALOG" | tail -1)
fi
DISPLAY=$DISPLAY_NUM import -window "$WIN" "$OUT"
echo "saved $OUT"
