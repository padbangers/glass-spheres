#!/usr/bin/env python3
"""Redraw the MANUAL tab's text in sdk/skins/ from skin/gen_layout.py, without the Docker skin pipeline.

The skin was rendered by the mpc-vst-plugins HTML art pipeline. This script redraws what gen_layout.py's
manual_page() describes:
  * each topic's text image (sh_mode_<tab>_<i>.png: title + lines), body text at MANUAL_SIZE;
  * the support lines (SUPPORT) under the topic list, into the tab background (sh_bg_<tab>.png).
The tab and the text colour come from sdk/skin.json ("manual": {"tab": n, "ink": "rrggbb"}).

Calibrated against a rendered skin (Overcast 1.0.6): the renderer draws ~4.3 % wider than Pillow at the same nominal
size, skin y = layout y - 86, and a text's baseline is at layout cy + 0.385 * px. The topic images' position is read
from TUI.json. Fonts: Titillium Web SemiBold (weight 600) and Bold (700), OFL, the skin's text face.

    python sdk/tools/manual_art.py <folder with TitilliumWeb-SemiBold.ttf and TitilliumWeb-Bold.ttf>

Idempotent: the first run keeps the original images in sdk/tools/orig/ and every run starts from them.
"""
import importlib.util
import json
import os
import re
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SKINS = os.path.join(HERE, "..", "skins")
ORIG = os.path.join(HERE, "orig")

SKIN_DY = -86                # skin y = layout y - 86
SCALE = 1.043                # HTML renderer px -> Pillow px
BASELINE = 0.385             # baseline below the text's cy, in px of its size
TITLE = (320, 214, 1.8, 700, "c83d58")
LINE_X, LINE_Y0, LINE_DY = 320, 254, 35
SUPPORT_X, SUPPORT_Y0, SUPPORT_DY, SUPPORT_COLOUR = 150, 600, 24, "c83d58"


def load_layout():
    spec = importlib.util.spec_from_file_location("gen_layout", os.path.join(REPO, "skin", "gen_layout.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def original(name):
    """The untouched image from the rendered skin (kept in tools/orig/ on the first run)."""
    os.makedirs(ORIG, exist_ok=True)
    keep = os.path.join(ORIG, name)
    if not os.path.exists(keep):
        shutil.copy2(os.path.join(SKINS, name), keep)
    return Image.open(keep)


def image_origin(name):
    """Where the skin places a topic image (its entry's bounds in TUI.json)."""
    s = open(os.path.join(SKINS, "TUI.json"), encoding="utf-8").read()
    i = s.index('"%s"' % name)
    m = re.search(r'"bounds": "(-?\d+) (-?\d+) \d+ \d+"', s[i:i + 2000])
    return int(m.group(1)), int(m.group(2))


def draw_text(draw, x, cy, label, size, weight, colour, fonts, anchor_x="l", dx=0, dy=0):
    px = size * 10
    font = ImageFont.truetype(fonts[weight], px * SCALE)
    baseline = cy + BASELINE * px
    draw.text((x + dx, baseline + dy), label, font=font, fill=rgb(colour), anchor=anchor_x + "s")
    return draw.textbbox((x + dx, baseline + dy), label, font=font, anchor=anchor_x + "s")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fonts = {600: os.path.join(sys.argv[1], "TitilliumWeb-SemiBold.ttf"),
             700: os.path.join(sys.argv[1], "TitilliumWeb-Bold.ttf")}
    cfg = json.load(open(os.path.join(HERE, "..", "skin.json")))["manual"]
    tab, ink = cfg["tab"], cfg["ink"]
    lay = load_layout()
    size = lay.MANUAL_SIZE

    # Topic images: same size as the rendered ones, transparent, title + lines.
    for i, (title, lines) in enumerate(lay.MANUAL):
        name = "sh_mode_%d_%d.png" % (tab, i)
        w, h = original(name).size
        ix, iy = image_origin(name)
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        dx, dy = -ix, SKIN_DY - iy
        x, cy, tsize, tweight, tcolour = TITLE
        draw_text(d, x, cy, title, tsize, tweight, tcolour, fonts, dx=dx, dy=dy)
        for j, line in enumerate(lines):
            if not line:
                continue
            box = draw_text(d, LINE_X, LINE_Y0 + j * LINE_DY, line, size, 600, ink, fonts, dx=dx, dy=dy)
            if box[2] > w or box[3] > h:
                sys.exit("%s line %d does not fit the %dx%d image: %s" % (title, j, w, h, box))
        im.save(os.path.join(SKINS, name), optimize=True)

    # Support lines under the topic list, in the tab background.
    bg_name = "sh_bg_%d.png" % tab
    bg = original(bg_name).convert("RGB")
    d = ImageDraw.Draw(bg)
    for j, line in enumerate(lay.SUPPORT):
        box = draw_text(d, SUPPORT_X, SUPPORT_Y0 + j * SUPPORT_DY, line, size, 700, SUPPORT_COLOUR, fonts,
                        anchor_x="m", dy=SKIN_DY)
        print("support line %d at %s" % (j, box))
    bg.save(os.path.join(SKINS, bg_name), optimize=True)
    print("redrew %d topic images and %s in %s" % (len(lay.MANUAL), bg_name, os.path.normpath(SKINS)))


if __name__ == "__main__":
    main()
