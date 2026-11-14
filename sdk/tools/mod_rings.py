#!/usr/bin/env python3
"""Show modulation on a plugin's main tab. Edits sdk/skins/TUI.json in place; running it again is harmless. In Overcast
run it after mode_dest_lists.py, which starts again from the original TUI.json.

What to change comes from sdk/skin.json:
  "views": {"page": "<main tab component>", "knob_def": "<regex of the knob components>",
            "knobs": ["<param key>", ...], "toggle_def": "<toggle component>", "toggle_key": "<param key>",
            "locks": {"<param key>": "<view suffix>", ...}}          (toggle and locks: optional)
The values are the read-only display parameters of sdk/src/views.h ("view_<x>", index NPARAMS + n), which the engine
fills from what the DSP really got last block:
  * knobs: a 65-frame filmstrip the size of the knob's strip, frame 0 empty (nothing modulates the knob), frames
    1..64 a dot on a circle in the strip's transparent margin, -135..+135 degrees like the knob's pointer. Drawn UNDER
    the knob image, so it never takes a touch or a drag from the knob.
  * the toggle (Freeze): the page gets it twice, bound to the same parameter: the normal component while
    view_<toggle_key> is 0, and a copy ("<toggle_def>_mod") whose buttons show the "on" image when off while it is 1.
    So the snowflake turns on while the matrix freezes, and every tap reaches a real button (an overlay above the
    button made it slow to react on the MPC X). Its "on" image gets a white outline like the "off" state's
    (*_border.png), so switching on doesn't look like a size jump. The conditions sit on the page entries as "Parameter N": inside a
    component, "IndexedEnabling/.../<local handle>" was not resolved and hid both buttons (MPC X, 2026-11-14).
  * locks: a knob the engine has switched off (view_<suffix> = 1, e.g. a RANDOM knob while the matrix drives its
    parameter) is hidden and a greyed copy of its strip, following the same parameter, is shown in its place.
Handles that are not mapped (the gain knobs share the knob component) hide their element: showWhenDataModelInvalid.

    python sdk/tools/mod_rings.py
"""
import json
import math
import os
import re
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SKINS = os.path.join(HERE, "..", "skins")
SRC = os.path.join(HERE, "..", "src")

RING_PNG = "sh_modring_%d.png"
GREY_PNG = "sh_grey_%s"
LOCK_DEF = "shLocked_%s"
FRAMES = 65
TEAL = (0x00, 0x97, 0x97)
TAG = "ModView"
OLD = ("shModToggle", "shModFreeze")   # earlier versions' overlay components
BORDER_SUFFIX = "_border.png"
BORDER_PX = 1.5   # the glyph has 1 px room at its top and bottom tips


def params():
    """param key -> index, from the generated params.h"""
    keys = re.findall(r'^    \{"(\w+)"', open(os.path.join(SRC, "params.h")).read(), re.M)
    return {k: i for i, k in enumerate(keys)}


def view_index(suffix):
    keys = re.findall(r'\{"(view_\w+)"', open(os.path.join(SRC, "views.h")).read())
    return len(params()) + keys.index("view_" + suffix)


def ring_strip(knob_png):
    """A dot strip for knobs drawn by knob_png (square frames stacked vertically). Returns its file name."""
    knob = Image.open(os.path.join(SKINS, knob_png)).convert("RGBA")
    size = knob.width
    c = size / 2.0
    # the knob body's radius: the first opaque pixel on the middle row of frame 0
    body = next(c - x for x in range(size // 2) if knob.getpixel((x, size // 2))[3] > 128)
    margin = c - body
    orbit = body + margin / 2.0
    dot = max(2.5, min(4.0, margin / 2.0 - 0.5))
    s = 4   # supersampled
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(1, FRAMES):
        big = Image.new("RGBA", (size * s, size * s), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        a = math.radians(-135.0 + 270.0 * (f - 1) / (FRAMES - 2))
        cx, cy = c + orbit * math.sin(a), c - orbit * math.cos(a)
        d.ellipse(((cx - dot - 1) * s, (cy - dot - 1) * s, (cx + dot + 1) * s, (cy + dot + 1) * s),
                  fill=(255, 255, 255, 230))
        d.ellipse(((cx - dot) * s, (cy - dot) * s, (cx + dot) * s, (cy + dot) * s), fill=TEAL + (255,))
        strip.alpha_composite(big.resize((size, size), Image.LANCZOS), (0, f * size))
    name = RING_PNG % size
    strip.save(os.path.join(SKINS, name), optimize=True)
    return name


def grey_strip(knob_png):
    """A greyed, faded copy of a knob strip. Returns its file name."""
    knob = Image.open(os.path.join(SKINS, knob_png)).convert("RGBA")
    rgb = ImageEnhance.Brightness(ImageEnhance.Color(knob.convert("RGB")).enhance(0.0)).enhance(1.25)
    grey = rgb.convert("RGBA")
    grey.putalpha(knob.getchannel("A").point(lambda a: int(a * 0.45)))
    name = GREY_PNG % knob_png
    grey.save(os.path.join(SKINS, name), optimize=True)
    return name


def bordered(png):
    """The "on" image with a white outline like the "off" state's, so switching on doesn't look like a size jump.
    Returns its file name; png may already be the bordered one (second run)."""
    base = png.replace(BORDER_SUFFIX, ".png")
    glyph = Image.open(os.path.join(SKINS, base)).convert("RGBA")
    s = 4   # supersampled: grow the shape by BORDER_PX
    big = glyph.getchannel("A").resize((glyph.width * s, glyph.height * s), Image.LANCZOS)
    big = big.point(lambda a: 255 if a > 100 else 0).filter(ImageFilter.MaxFilter(2 * int(BORDER_PX * s) + 1))
    ring = Image.new("RGBA", glyph.size, (255, 255, 255, 0))
    ring.putalpha(big.resize(glyph.size, Image.LANCZOS))
    ring.alpha_composite(glyph)
    name = base.replace(".png", BORDER_SUFFIX)
    ring.save(os.path.join(SKINS, name), optimize=True)
    return name


def image_strip(image, frames, bounds, handle):
    return {"version": 2,
            "componentData": {"version": 1, "name": TAG, "type": "Image",
                              "data": {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": image,
                                       "numFrames": frames, "handleName": handle}},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide",
                       "whenVisible": "Always", "boundsType": "Absolute", "bounds": bounds,
                       "additionalInvalidatingHandles": []}}


def plain_def(key, children):
    return {"key": key, "value": {
        "version": 4, "actions": [],
        "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
                           "unfocussed": {"version": 1, "colour": "0", "image": ""}},
        "ignoreMousePresses": True, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": False,
        "componentsData": children}}


def when(component, cond):
    component["bounds"]["version"] = 2
    component["bounds"]["additionalInvalidatingHandles"] = component["bounds"].get("additionalInvalidatingHandles", []) + [cond]


def main():
    cfg = json.load(open(os.path.join(HERE, "..", "skin.json")))["views"]
    knob_def = re.compile(cfg["knob_def"])
    pidx = params()
    knob_views = {pidx[k]: k for k in cfg["knobs"]}
    toggle = "toggle_key" in cfg
    toggle_param = pidx[cfg["toggle_key"]] if toggle else -1
    locks = {pidx[k]: v for k, v in cfg.get("locks", {}).items()}
    toggle_mod = cfg["toggle_def"] + "_mod" if toggle else None

    path = os.path.join(SKINS, "TUI.json")
    tui = json.load(open(path, encoding="utf-8"))
    local = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    local[:] = [e for e in local if e["key"] not in OLD and e["key"] != toggle_mod and not e["key"].startswith("shLocked_")]
    by_key = {e["key"]: e for e in local}

    # knob components: the dot strip under the knob
    knobs, knob_strip = 0, {}
    for e in local:
        comps = [c for c in e["value"]["componentsData"] if c["componentData"].get("name") != TAG]
        if knob_def.match(e["key"]):
            at = next(i for i, c in enumerate(comps) if c["componentData"].get("name") == "Knob")
            knob_strip[e["key"]] = comps[at]
            comps.insert(at, image_strip(ring_strip(comps[at]["componentData"]["data"]["filmStrip"]), FRAMES,
                                         comps[at]["bounds"]["bounds"], "View"))
            knobs += 1
        e["value"]["componentsData"] = comps

    # the toggle's "on" image gets a white outline; the lit toggle: a copy whose buttons show it also when off
    if toggle:
        for c in by_key[cfg["toggle_def"]]["value"]["componentsData"]:
            if c["componentData"].get("type") == "Button":
                c["componentData"]["data"]["onImage"] = bordered(c["componentData"]["data"]["onImage"])
        mod = json.loads(json.dumps(by_key[cfg["toggle_def"]]))
        mod["key"] = toggle_mod
        for c in mod["value"]["componentsData"]:
            if c["componentData"].get("type") == "Button":
                c["componentData"]["data"]["offImage"] = c["componentData"]["data"]["onImage"]
        local.append(mod)

    page_def = by_key[cfg["page"]]["value"]
    lock_conds = {"IndexedEnabling/0/2/Parameter %d" % view_index(v) for v in list(locks.values()) + ([cfg["toggle_key"]] if toggle else [])}
    out, mapped, locked = [], 0, 0
    for c in page_def["componentsData"]:
        t = c["componentData"].get("type")
        if c["componentData"].get("name") == TAG or t in OLD:
            continue
        if toggle and t == toggle_mod:   # the original entry, as an earlier version of this script left it
            c["componentData"]["type"] = t = cfg["toggle_def"]
        m = c["handle remapping"]["map"]
        m[:] = [x for x in m if x["key"] != "View"]
        b = c["bounds"]
        if "additionalInvalidatingHandles" in b:
            b["additionalInvalidatingHandles"] = [h for h in b["additionalInvalidatingHandles"] if h not in lock_conds]
        data = next((x["value"] for x in m if x["key"] == "Data"), "")
        p = int(data.split()[-1]) if data.startswith("Parameter ") else -1
        out.append(c)
        if knob_def.match(t) and p in knob_views:
            m.append({"key": "View", "value": "Parameter %d" % view_index(knob_views[p])})
            mapped += 1
        elif toggle and t == cfg["toggle_def"] and p == toggle_param:
            v = view_index(cfg["toggle_key"])
            lit = json.loads(json.dumps(c))
            lit["componentData"]["type"] = toggle_mod
            lit["componentData"]["name"] = TAG
            when(c, "IndexedEnabling/0/2/Parameter %d" % v)
            when(lit, "IndexedEnabling/1/2/Parameter %d" % v)
            out.append(lit)
            mapped += 1
        if knob_def.match(t) and p in locks:
            v = view_index(locks[p])
            when(c, "IndexedEnabling/0/2/Parameter %d" % v)       # the live knob only while unlocked
            knob = knob_strip[t]
            strip = knob["componentData"]["data"]["filmStrip"]
            grey = grey_strip(strip)
            key = LOCK_DEF % grey.replace(".png", "")
            if key not in {e["key"] for e in local}:
                local.append(plain_def(key, [image_strip(grey, knob["componentData"]["data"]["numFrames"],
                                                         "0 0 %s %s" % tuple(knob["bounds"]["bounds"].split()[2:]),
                                                         "Data")]))
            px, py = (int(x) for x in b["bounds"].split()[:2])
            kx, ky, kw, kh = (int(x) for x in knob["bounds"]["bounds"].split())
            g = {"version": 2,
                 "componentData": {"version": 1, "name": TAG, "type": key, "data": {"version": 1, "handleName": "Data"}},
                 "handle remapping": {"version": 1, "map": [{"key": "Data", "value": data}]},
                 "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Hide",
                            "whenVisible": "Always", "boundsType": "Absolute",
                            "bounds": "%d %d %d %d" % (px + kx, py + ky, kw, kh),
                            "additionalInvalidatingHandles": ["IndexedEnabling/1/2/Parameter %d" % v]}}
            out.append(g)
            locked += 1
    page_def["componentsData"] = out
    for f in ("sh_modtoggle.png", "sh_modfreeze.png"):
        if os.path.exists(os.path.join(SKINS, f)):
            os.remove(os.path.join(SKINS, f))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(tui, f, ensure_ascii=False, separators=(", ", ": "))
    print("rings in %d knob components, %d controls bound, %d lockable knobs" % (knobs, mapped, locked))
    if mapped != len(knob_views) + toggle or locked != len(locks):
        sys.exit("expected %d bound controls and %d locks" % (len(knob_views) + toggle, len(locks)))


if __name__ == "__main__":
    main()
