#!/usr/bin/env python3
"""
Patch the Shanling M3X device tree inside the stock boot image.

Why
---
The M3X has two AKM AK4497 DACs on i2c bus 5 (soc/i2c@7af5000, slaves 0x10
and 0x12). That bus needs TLMM pins **gpio18 and gpio19**, muxed as
`blsp_i2c5`:

    pinctrl@1000000 {
        i2c_5 {
            i2c_5_active { mux { pins = "gpio18", "gpio19"; function = "blsp_i2c5"; } };
            i2c_5_sleep  { mux { pins = "gpio18", "gpio19"; function = "gpio";       } };
        };
    };

But the gpio-keys driver selects this group:

    tlmm_gpio_key {
        gpio_key_active {
            mux   { pins = "gpio13", "gpio17", "gpio91", "gpio1", "gpio29",
                              "gpio30", "gpio18", "gpio19", "gpio65"; function = "gpio"; };
            config{ pins = <same nine>; drive-strength = <0x02>; bias-pull-up; };
        };
        gpio_key_suspend { ...same nine... };
    };

...even though soc/gpio_keys only ever *uses* two of those pins:

    vol_down { gpios = <... 0x0d ...>; linux,code = <0xa4>; };   /* gpio13 */
    vol_up   { gpios = <... 0x11 ...>; linux,code = <0xa5>; };   /* gpio17 */

Because msm pinctrl claims every pin listed in a group, gpio-keys holds gpio18
and gpio19 and the DAC bus can never be muxed. The kernel says exactly that:

    msm8937-pinctrl: pin GPIO_18 already requested by soc:gpio_keys;
                     cannot claim for 7af5000.i2c
    i2c-msm-v2 7af5000.i2c: Error applying setting, reverse things back
    i2c-msm-v2 7af5000.i2c: error pinctrl_select_state(i2c_sleep) err:-22

Consequences: the AK4497s cannot be configured, `i2cdump` cannot read their
registers, and the Quinary MI2S route refuses its hw parameters so nothing ever
reaches the DACs.

Fix
---
Drop gpio18 and gpio19 from `tlmm_gpio_key/gpio_key_active` and
`gpio_key_suspend` (both the `mux` and `config` pin lists). The two keys keep
working on gpio13/gpio17, and I2C5 gets its pins.

This script rewrites the stock boot image in place. It does not need kernel
sources: the M3X boot image's kernel blob is `gzip(ARM64 Image)` followed by a
bank of 68 concatenated DTBs, one per board variant, and the M3X's is the one
with model "Qualcomm Technologies, Inc. MSM8937-PMI8950 MTP". Only that DTB's
bytes change; its size is preserved by padding so every later DTB keeps its
offset.

Usage:
    python3 tools/m3x/patch-dtb-gpio18.py firmware/m3x_v1_75/boot.img out/boot.img

The result is a *stock* boot image (no Magisk). Patch it with Magisk afterwards
before flashing.
"""

import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import zlib

FDT_MAGIC = 0xD00DFEED
M3X_MODEL = b"Qualcomm Technologies, Inc. MSM8937-PMI8950 MTP"
CONFLICT_PINS = ("gpio18", "gpio19")


def die(msg):
    sys.exit("error: %s" % msg)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# ---------------------------------------------------------------- boot image

def parse_boot(img):
    """Return (page_size, kernel_size, ramdisk_size, kernel, ramdisk).

    Android boot image header v0 layout:
        0x00 magic "ANDROID!"   0x08 kernel_size   0x0c kernel_addr
        0x10 ramdisk_size        0x14 ramdisk_addr  0x18 second_size
        0x1c second_addr         0x20 tags_addr     0x24 page_size
        0x28 dt_size             0x2c unused
    """
    if img[:8] != b"ANDROID!":
        die("not an Android boot image")
    page = struct.unpack("<I", img[0x24:0x28])[0]
    ksize = struct.unpack("<I", img[0x08:0x0C])[0]
    rsize = struct.unpack("<I", img[0x10:0x14])[0]
    if not page:
        die("bad page_size")
    off = page
    kernel = img[off:off + ksize]
    off += (ksize + page - 1) // page * page
    ramdisk = img[off:off + rsize]
    return page, ksize, rsize, kernel, ramdisk


# ---------------------------------------------------------------- DTB bank

def split_dtb_bank(blob):
    """Split a run of back-to-back FDTs into [(offset, size), ...]."""
    out = []
    off = 0
    while True:
        i = blob.find(struct.pack(">I", FDT_MAGIC), off)
        if i < 0:
            break
        size = struct.unpack(">I", blob[i + 4:i + 8])[0]
        if 0x1000 < size < 0x400000 and i + size <= len(blob):
            out.append((i, size))
            off = i + size
        else:
            off = i + 1
    return out


def fdt_model(dtb):
    """Pull the root 'model' string out of a DTB without dtc."""
    magic, totalsize, off_struct, off_strings = struct.unpack(">4I", dtb[:16])
    strings = dtb[off_strings:off_strings + struct.unpack(">I", dtb[32:36])[0]]
    depth = 0
    p = off_struct
    end = off_struct + struct.unpack(">I", dtb[36:40])[0]
    while p < end and p + 4 <= len(dtb):
        tok = struct.unpack(">I", dtb[p:p + 4])[0]
        p += 4
        if tok == 1:                      # FDT_BEGIN_NODE
            depth += 1
            nul = dtb.index(b"\x00", p)
            name = dtb[p:nul]
            p = (nul + 4) & ~3
            if depth == 1:
                root = name
        elif tok == 2:                    # FDT_END_NODE
            depth -= 1
        elif tok == 3:                    # FDT_PROP
            plen, nameoff = struct.unpack(">II", dtb[p:p + 8])
            p += 8
            val = dtb[p:p + plen]
            p = (p + plen + 3) & ~3
            if depth == 1 and strings[nameoff:strings.index(b"\x00", nameoff)] == b"model":
                return val.rstrip(b"\x00")
        elif tok == 4:                    # FDT_NOP
            pass
        elif tok == 9:                    # FDT_END
            break
    return b""


# ---------------------------------------------------------------- the patch

def patch_dts(dts_path):
    """Remove the conflicting pins from tlmm_gpio_key."""
    text = open(dts_path, encoding="utf-8", errors="replace").read()

    start = text.find("tlmm_gpio_key {")
    if start < 0:
        die("tlmm_gpio_key not found")

    # Walk to the matching close brace rather than guessing at indentation:
    # tlmm_gpio_key is nested several levels deep and its parent pinctrl node
    # also holds the i2c_5_* groups, which must NOT be touched.
    depth = 0
    end = None
    for i in range(start, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        die("could not find the end of tlmm_gpio_key")
    block = text[start:end]

    changed = 0
    for pin in CONFLICT_PINS:
        for pat in ('"%s", ' % pin, ', "%s"' % pin):
            n = block.count(pat)
            if n:
                block = block.replace(pat, "")
                changed += n
    if changed == 0:
        die("no conflicting pins found in tlmm_gpio_key (already patched?)")
    # Two groups (gpio_key_active, gpio_key_suspend) x two pin lists each
    # (mux, config) x two pins = 8.
    if changed != 8:
        die("expected to remove 8 pin entries, removed %d - refusing to guess"
            % changed)

    print("  tlmm_gpio_key: removed %d pin entries (%s)"
          % (changed, ", ".join(CONFLICT_PINS)))
    print("  resulting pin lists:")
    for line in block.splitlines():
        if "pins =" in line:
            print("    " + line.strip())

    return text[:start] + block + text[end:]


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)

    src, dst = sys.argv[1], sys.argv[2]
    # Optional third argument: a Magisk-patched boot image to patch instead of
    # the stock one. Magisk only rewrites the ramdisk - it leaves the kernel
    # region byte-identical - so the DTB patch can be applied to either and the
    # result keeps root.
    base = sys.argv[3] if len(sys.argv) == 4 else None
    if base:
        img = open(base, "rb").read()
        print("base boot image: %s (%d bytes)  [Magisk-patched]" % (base, len(img)))
        # The DTB lives inside the kernel, so what has to match is the kernel
        # region - not the total image size (Magisk grows the ramdisk).
        stock = open(src, "rb").read()
        spage = struct.unpack("<I", stock[0x24:0x28])[0]
        sks = struct.unpack("<I", stock[0x08:0x0C])[0]
        bpage = struct.unpack("<I", img[0x24:0x28])[0]
        bks = struct.unpack("<I", img[0x08:0x0C])[0]
        if (spage, sks) != (bpage, bks):
            die("kernel region differs: stock page/size %d/%d vs base %d/%d"
                % (spage, sks, bpage, bks))
        if stock[spage:spage + sks] != img[bpage:bpage + bks]:
            die("the base image's kernel is not the stock kernel; refusing to "
                "splice a DTB patch built from a different source")
    else:
        img = open(src, "rb").read()
        print("source boot image: %s (%d bytes)" % (src, len(img)))
    print("  sha256 %s" % sha256(img))

    page, ksize, rsize, kernel, ramdisk = parse_boot(img)
    cmdline = ""
    nul = img.find(b"\x00", 64)
    cmdline = img[64:nul].decode("ascii", "replace")
    print("  page_size=%d kernel_size=%d ramdisk_size=%d" % (page, ksize, rsize))
    print("  cmdline: %s" % (cmdline[:90] + ("..." if len(cmdline) > 90 else "")))

    # The kernel blob is gzip(ARM64 Image) followed by the DTB bank.
    if kernel[:3] != b"\x1f\x8b\x08":
        die("kernel is not gzip wrapped; unexpected layout")
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    image = d.decompress(kernel) + d.flush()
    tail = d.unused_data
    print("  Image: %d bytes, DTB bank: %d bytes" % (len(image), len(tail)))

    bank = split_dtb_bank(tail)
    print("  DTB bank holds %d device trees" % len(bank))

    target = None
    for n, (off, size) in enumerate(bank):
        dtb = tail[off:off + size]
        if fdt_model(dtb) == M3X_MODEL:
            target = n
            break
    if target is None:
        die("no DTB with model %r found in the bank" % M3X_MODEL.decode())
    off, size = bank[target]
    print("  M3X DTB is #%d at tail offset 0x%x, %d bytes" % (target, off, size))

    with tempfile.TemporaryDirectory() as td:
        orig = os.path.join(td, "orig.dtb")
        dts = os.path.join(td, "m3x.dts")
        newdtb = os.path.join(td, "new.dtb")
        open(orig, "wb").write(tail[off:off + size])

        subprocess.run(["dtc", "-I", "dtb", "-O", "dts", "-o", dts, orig],
                       stderr=subprocess.DEVNULL, check=True)
        print("patching:")
        patched = patch_dts(dts)
        open(dts, "w", encoding="utf-8").write(patched)

        subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", newdtb, dts],
                       stderr=subprocess.DEVNULL, check=True)
        new = open(newdtb, "rb").read()

    print("  recompiled DTB: %d bytes (was %d)" % (len(new), size))
    if len(new) > size:
        die("recompiled DTB is %d bytes too large to fit in place" % (len(new) - size))

    # Preserve the original size so every later DTB keeps its offset. The FDT
    # header's totalsize still describes only the real content.
    new_dt = new + b"\x00" * (size - len(new))
    tail2 = tail[:off] + new_dt + tail[off + size:]
    if len(tail2) != len(tail):
        die("DTB bank size changed")

    print("  splicing the patched DTB back into the kernel blob")
    gz_len = len(kernel) - len(tail)
    new_kernel = kernel[:gz_len] + tail2
    if len(new_kernel) != len(kernel):
        die("kernel blob size changed by %d bytes" % (len(new_kernel) - len(kernel)))

    # Splice the new kernel into a copy of the *original* image, so the OEM
    # boot header, cmdline, name and ids stay byte-for-byte identical.
    out = bytearray(img)
    out[page:page + ksize] = new_kernel

    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    out = bytes(out)
    open(dst, "wb").write(out)
    print("wrote %s (%d bytes)" % (dst, len(out)))
    print("  sha256 %s" % sha256(out))
    print("  %d of %d bytes differ from the source"
          % (sum(1 for a, b in zip(img, out) if a != b), len(img)))
    print()
    if base:
        print("This image already carries the Magisk patch, so it is ready to")
        print("flash. Pad it to the full boot partition and write to LBA 790528.")
    else:
        print("This is a STOCK boot image. Patch it with Magisk before flashing,")
        print("then write it to the boot partition (LBA 790528, 131072 sectors).")


if __name__ == "__main__":
    main()