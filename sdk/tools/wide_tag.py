#!/usr/bin/env python3
"""Widen the "Y" tag on the Y / CLOCK tab so it spans the whole Y section: the knob column (Y SPREAD, Y BIAS,
Y STEPS) and the list column (Y DIVIDER, Y RANGE, X CLOCK). In 1.0.0 it sat between the two columns, which looked
misplaced next to CLOCK and GATES (one column each).

The tag is part of the tab's rendered background (sh_bg_1.png). This script draws a wide teal pill there with the
pill's own edge (skin/pill_teal.png, middle stretched) and moves the rendered "Y" from the old pill to its centre.
The background is flat horizontally, and the new pill covers the old one, so nothing else needs repainting.
skin/gen_layout.py draws the same (YC_Y_TAG, skin/pill_teal_wide.png) for the Docker route.

    python sdk/tools/wide_tag.py

Idempotent: the first run keeps the original image in sdk/tools/orig/ and every run starts from it.
"""
import os
import shutil

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SKINS = os.path.join(HERE, "..", "skins")
ORIG = os.path.join(HERE, "orig")

BG = "sh_bg_1.png"
OLD = (190, 109, 132, 26)        # x, y, w, h of the 1.0.0 tag in the skin (layout cx 256, cy 208; skin y = layout - 86)
NEW_X, NEW_W = 62, 418           # from Y SPREAD's tag (cx 128 - 66) to the lists' right edge (cx 384 + 95)
TEXT_W = 52                      # the middle of the old pill that holds the "Y"


def wide_pill(w, h):
    """skin/pill_teal.png at width w: its rounded ends kept, the middle stretched."""
    pill = Image.open(os.path.join(REPO, "skin", "pill_teal.png")).convert("RGBA").resize((OLD[2], h))
    cap = h // 2
    out = Image.new("RGBA", (w, h))
    out.paste(pill.crop((0, 0, cap, h)), (0, 0))
    out.paste(pill.crop((cap, 0, cap + 1, h)).resize((w - 2 * cap, h)), (cap, 0))
    out.paste(pill.crop((OLD[2] - cap, 0, OLD[2], h)), (w - cap, 0))
    return out


def main():
    os.makedirs(ORIG, exist_ok=True)
    keep = os.path.join(ORIG, BG)
    if not os.path.exists(keep):
        shutil.copy2(os.path.join(SKINS, BG), keep)
    bg = Image.open(keep).convert("RGBA")
    x, y, w, h = OLD
    label = bg.crop((x + (w - TEXT_W) // 2, y, x + (w + TEXT_W) // 2, y + h))
    bg.alpha_composite(wide_pill(NEW_W, h), (NEW_X, y))
    bg.paste(label, (NEW_X + (NEW_W - TEXT_W) // 2, y))
    bg.convert("RGB").save(os.path.join(SKINS, BG), optimize=True)
    # the same pill for skin/gen_layout.py
    wide_pill(NEW_W, h).save(os.path.join(REPO, "skin", "pill_teal_wide.png"), optimize=True)
    print("Y tag: x %d..%d (was %d..%d)" % (NEW_X, NEW_X + NEW_W, x, x + w))


if __name__ == "__main__":
    main()
