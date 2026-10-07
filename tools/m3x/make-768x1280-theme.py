#!/usr/bin/env python3
"""
Generate a Cabbie v2 theme for the Shanling M3X's 768x1280 panel.

Rockbox ships no theme for 768x1280, so /-rockbox build produces an empty
.rockbox/backdrops, an empty .rockbox/wps/cabbiev2 and no tango icons, and the
running build logs:

    read_bmp_file: can't open '/.rockbox/icons/tango_icons.32x32.bmp', rc: -1
    read_bmp_file: can't open '/.rockbox/icons/tango_icons_viewers.32x32.bmp', rc: -1

768x1280 and the shipped portrait Cabbie v2 skin (480x800) have the same 0.6
aspect ratio and 480x800 * 1.6 == 768x1280 exactly, so the skin can be derived
mechanically:

  * every bitmap referenced from the WPS is scaled by 1.6,
  * the backdrop is scaled by 1.6,
  * the WPS' pixel coordinates are scaled by 1.6,
  * WPSLIST gains 768x1280x(16|24|32) entries.

wpsbuild.pl copies bitmaps verbatim (copybackdrop/copywps), so correctly sized
sources have to exist; it does not rescale them.

Run from the repository root:

    python3 tools/m3x/make-768x1280-theme.py

Then re-run configure (the WPSLIST change is read by `make zip`) and
`make zip`.
"""

import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: python3 -m pip install --user Pillow")

# <workspace>/tools/m3x/<this file>  ->  <workspace>/rockbox
ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "rockbox",
)
WPS_DIR = os.path.join(ROOT, "wps")
THEME_DIR = os.path.join(WPS_DIR, "cabbiev2")
BACKDROPS = os.path.join(ROOT, "backdrops")
WPSLIST = os.path.join(WPS_DIR, "WPSLIST")

SRC_W, SRC_H = 480, 800
DST_W, DST_H = 768, 1280
SCALE = DST_W / SRC_W          # 1.6, identical on both axes

RES_KEY = "%dx%dx16" % (SRC_W, SRC_H)
RES_NEW = "%dx%dx16" % (DST_W, DST_H)

# WPS tags whose arguments are (x, y, w, h) and must be scaled.
# Each entry: tag -> (offset of the first coordinate field, how many to scale).
# %Vl is the odd one out: its first argument is a viewport label, not x.
SCALED_COORD_TAGS = {
    "%Vl": (1, 4),   # %Vl(<label>, x, y, w, h)
    "%V": (0, 4),    # %V(x, y, w, h)
    "%T": (0, 4),    # %T(x, y, w, h, id)
    "%Cl": (0, 4),   # %Cl(x, y, w, h, base, colour)
    "%pb": (0, 4),   # %pb(x, y, w, h, bitmap)
    "%pv": (0, 4),   # %pv(x, y, w, h, ...)
}


def scale_bitmap(src, dst):
    """Scale a bitmap by SCALE and write it back out.

    The WPS bitmaps are individual sprites (a 70x70 volume icon, a 452x205
    popup, ...), so each one is scaled by the factor rather than resized to a
    fixed target; only the backdrop is full screen.
    """
    im = Image.open(src)
    w, h = im.size
    tw, th = max(1, round(w * SCALE)), max(1, round(h * SCALE))
    if (w, h) == (tw, th):
        print("  %-44s already %dx%d" % (os.path.basename(src), tw, th))
        return
    out = im.resize((tw, th), Image.LANCZOS)
    out.save(dst, format="BMP")
    print("  %-44s %dx%d -> %dx%d" % (os.path.basename(src), w, h, tw, th))


def scale_coord(value):
    """Scale one WPS coordinate field, preserving '-' (auto) and fractions."""
    v = value.strip()
    if v == "-" or v == "":
        return value
    try:
        n = float(v)
    except ValueError:
        return value
    if n == int(n):
        return str(int(round(n * SCALE)))
    return "%.4g" % (n * SCALE)


def coord_offset(tag, args):
    """Index of the first coordinate field.

    %Vl always starts with a label; %T may start with an optional touch-region
    id ("%T(small_vol,145,...)"), so for the rest the offset is decided by
    whether the first field parses as a number.
    """
    if tag == "%Vl":
        return 1
    try:
        float(args[0].strip())
        return 0
    except (ValueError, IndexError):
        return 1


def scale_line(line):
    """Scale the coordinate fields of a single WPS line."""
    # %xl(<label>,<bitmap>,<x>,<y>[,<frame>]) - the coordinates sit *after* the
    # bitmap name, so this needs its own handling.
    m = re.search(r"%xl\(", line)
    if m:
        start = m.end()
        end = line.find(")", start)
        if end > 0:
            args = line[start:end].split(",")
            if len(args) >= 4 and args[1].strip().lower().endswith(".bmp"):
                args[2] = scale_coord(args[2])
                args[3] = scale_coord(args[3])
                return line[:start] + ",".join(args) + line[end:]

    for tag, (_default, nfields) in SCALED_COORD_TAGS.items():
        idx = line.find(tag + "(")
        if idx < 0:
            continue
        start = idx + len(tag) + 1
        end = line.find(")", start)
        if end < 0:
            continue
        args = line[start:end].split(",")
        offset = coord_offset(tag, args)
        if len(args) < offset + nfields:
            continue
        for i in range(offset, offset + nfields):
            args[i] = scale_coord(args[i])
        return line[:start] + ",".join(args) + line[end:]

    return line


def main():
    src_wps = os.path.join(WPS_DIR, "cabbiev2.%dx%dx16.wps" % (SRC_W, SRC_H))
    dst_wps = os.path.join(WPS_DIR, "cabbiev2.%dx%dx16.wps" % (DST_W, DST_H))

    print("WPS bitmaps:")
    if not os.path.isdir(THEME_DIR):
        sys.exit("missing %s" % THEME_DIR)
    for name in sorted(os.listdir(THEME_DIR)):
        if not name.endswith("-%s.bmp" % RES_KEY):
            continue
        dst_name = name.replace("-%s.bmp" % RES_KEY, "-%s.bmp" % RES_NEW)
        scale_bitmap(os.path.join(THEME_DIR, name), os.path.join(THEME_DIR, dst_name))

    print("Backdrop:")
    src_bd = os.path.join(BACKDROPS, "cabbiev2.%s.bmp" % RES_KEY)
    dst_bd = os.path.join(BACKDROPS, "cabbiev2.%s.bmp" % RES_NEW)
    if not os.path.exists(src_bd):
        sys.exit("missing %s" % src_bd)
    scale_bitmap(src_bd, dst_bd)

    print("WPS:")
    if not os.path.exists(src_wps):
        sys.exit("missing %s" % src_wps)
    out = []
    for line in open(src_wps, encoding="utf-8"):
        # The skin references its bitmaps by explicit resolution in the name.
        line = line.replace("-%s.bmp" % RES_KEY, "-%s.bmp" % RES_NEW)
        # Keep the leading comment block but note where it came from.
        out.append(scale_line(line.rstrip("\n")))
    body = "\n".join(out) + "\n"

    header = [
        "# Cabbie v2.0 for the Shanling M3X (768x1280)",
        "# Generated from cabbiev2.%dx%dx16.wps by tools/m3x/make-768x1280-theme.py" % (SRC_W, SRC_H),
        "# All coordinates and bitmaps scaled by %g." % SCALE,
        "",
    ]
    # Drop the original comment block (leading '#' lines) and re-emit ours.
    first = next(i for i, l in enumerate(out) if not l.startswith("#"))
    open(dst_wps, "w", encoding="utf-8").write(
        "\n".join(header + [l for l in out[first:]]) + "\n"
    )
    print("  wrote %s" % os.path.relpath(dst_wps, ROOT))

    print("WPSLIST:")
    text = open(WPSLIST, encoding="utf-8").read()
    key = "%dx%dx(16|24|32)" % (DST_W, DST_H)
    if "wps.%s:" % key in text:
        print("  entry for %s already present" % key)
        return

    additions = [
        ("# Shanling M3X (768x1280) - generated by tools/m3x/make-768x1280-theme.py",
         None),
        ("wps.%s:  cabbiev2.%dx%dx16.wps" % (key, DST_W, DST_H),
         "wps.480x800x(16|24|32):  cabbiev2.480x800x16.wps"),
        ("Font.%s: 35-Adobe-Helvetica.fnt" % key,
         "Font.480x800x(16|24|32): 35-Adobe-Helvetica.fnt"),
        ("backdrop.%s: backdrops/cabbiev2.%dx%dx16.bmp" % (key, DST_W, DST_H),
         "backdrop.480x800x(16|24|32): backdrops/cabbiev2.480x800x16.bmp"),
        ("iconset.%s:  icons/tango_icons.32x32.bmp" % key,
         "iconset.480x800x(16|24|32):  icons/tango_icons.32x32.bmp"),
        ("viewers iconset.%s:  icons/tango_icons_viewers.32x32.bmp" % key,
         "viewers iconset.480x800x(16|24|32):  icons/tango_icons_viewers.32x32.bmp"),
    ]
    for line, anchor in additions:
        if anchor is None:
            text = text.rstrip("\n") + "\n\n" + line + "\n"
        elif anchor in text:
            text = text.replace(anchor, anchor + "\n" + line, 1)
        else:
            print("  WARNING: anchor not found: %s" % anchor)
    open(WPSLIST, "w", encoding="utf-8").write(text)
    print("  added 4 entries for %s" % key)


if __name__ == "__main__":
    main()