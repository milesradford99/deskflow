#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Deskflow Developers
# SPDX-License-Identifier: GPL-2.0-only
"""Regenerate every Deskflow app icon from the SVG sources in this folder.

  deskflow.svg           full-colour app icon (1024 grid, macOS-style padding)
  deskflow-small.svg     bolder, simplified version for 32px and below
  deskflow-symbolic.svg  single-colour mark used for the symbolic tray icon
  dmg-volume.svg         macOS disk image volume icon

Requires: pip install cairosvg pillow
Run from anywhere: python3 src/apps/res/icons/source/generate_icons.py
"""
import io
from pathlib import Path

import cairosvg
from PIL import Image

SRC = Path(__file__).resolve().parent
RES = SRC.parent.parent
ROOT = RES.parent.parent.parent

# Windows, Linux and in-app icons fill more of the canvas than the macOS grid.
TIGHT_VIEWBOX = "72 72 880 880"
SYMBOLIC_COLORS = {"light": "#232629", "dark": "#fcfcfc"}
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SMALL_MAX = 32  # sizes up to this use the simplified artwork


def render(svg: str, size: int) -> Image.Image:
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def tight(svg: str) -> str:
    return svg.replace('viewBox="0 0 1024 1024"', f'viewBox="{TIGHT_VIEWBOX}"', 1)


def main() -> None:
    app = (SRC / "deskflow.svg").read_text()
    small = (SRC / "deskflow-small.svg").read_text()
    symbolic = (SRC / "deskflow-symbolic.svg").read_text()
    dmg = (SRC / "dmg-volume.svg").read_text()
    app_tight, small_tight = tight(app), tight(small)

    def art(size: int) -> Image.Image:
        return render(small_tight if size <= SMALL_MAX else app_tight, size)

    # In-app SVGs: window title bar, colour tray icon and symbolic tray icon.
    # These are mostly drawn at 16-32px, so they use the simplified artwork.
    for theme, color in SYMBOLIC_COLORS.items():
        apps = RES / f"icons/deskflow-{theme}/apps/64"
        (apps / "org.deskflow.deskflow.svg").write_text(
            small_tight.replace('width="1024" height="1024"', 'width="64" height="64"', 1)
        )
        (apps / "org.deskflow.deskflow-symbolic.svg").write_text(symbolic.replace("#232629", color))

    # Windows .ico, shared by the GUI, core and daemon executables
    images = {s: art(s) for s in ICO_SIZES}
    images[256].save(
        RES / "deskflow.ico",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=[images[s] for s in ICO_SIZES[:-1]],
    )

    # macOS .icns keeps the Apple grid padding
    icns_small = [render(small, s) for s in (16, 32, 64)]
    icns_big = [render(app, s) for s in (128, 256, 512)]
    render(app, 1024).save(RES / "Deskflow.icns", append_images=icns_small + icns_big)
    render(dmg, 1024).save(ROOT / "deploy/mac/dmg-volume.icns")

    # Linux hicolor 512px icon, and the small logo used in the developer docs
    render(app_tight, 512).save(ROOT / "deploy/linux/org.deskflow.deskflow.png", optimize=True)
    render(app_tight, 64).save(ROOT / "docs/dev/deskflow-logo.png", optimize=True)


if __name__ == "__main__":
    main()
