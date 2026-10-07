"""Read-only validation of the existing GPIO18/19 boot-image patch."""
from pathlib import Path
import importlib.util
import struct
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("dtb_patch", ROOT / "tools/m3x/patch-dtb-gpio18.py")
PATCH = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PATCH)


def properties(dtb):
    header = struct.unpack_from(">10I", dtb)
    strings = dtb[header[3]:header[3] + header[8]]
    offset, end = header[2], header[2] + header[9]
    stack, result = [], {}
    while offset < end:
        token = struct.unpack_from(">I", dtb, offset)[0]
        offset += 4
        if token == 1:
            stop = dtb.index(0, offset)
            stack.append(dtb[offset:stop].decode())
            offset = (stop + 4) & ~3
        elif token == 2:
            stack.pop()
        elif token == 3:
            size, name = struct.unpack_from(">II", dtb, offset)
            offset += 8
            name = strings[name:strings.index(0, name)].decode()
            result[("/".join(stack), name)] = dtb[offset:offset + size]
            offset = (offset + size + 3) & ~3
        elif token == 9:
            break
        else:
            assert token == 4, token
    return result


BOOT_FIXTURES = (
    ROOT / "firmware/m3x_v1_75/boot.img",
    ROOT / "firmware/m3x_v1_75/boot-dtbpatch.img",
    ROOT / "firmware/m3x_v1_75/boot-magisk-dtb.img",
    ROOT / "backups/magisk_patched-30700_25tcf.img",
)


@unittest.skipUnless(all(path.is_file() for path in BOOT_FIXTURES),
                     "Private stock/rooted boot-image fixtures are not distributed")
class BootImageTests(unittest.TestCase):
    def test_stock_patch_preserves_kernel_ramdisk_and_other_boards(self):
        base = ROOT / "firmware/m3x_v1_75"
        original = (base / "boot.img").read_bytes()
        patched = (base / "boot-dtbpatch.img").read_bytes()
        page, size, _, kernel, ramdisk = PATCH.parse_boot(original)
        _, _, _, kernel2, ramdisk2 = PATCH.parse_boot(patched)
        self.assertEqual(len(original), len(patched))
        self.assertEqual(original[:page], patched[:page])
        self.assertEqual(original[page + size:], patched[page + size:])
        self.assertEqual(ramdisk, ramdisk2)
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        image = d.decompress(kernel) + d.flush()
        bank = d.unused_data
        compressed_size = len(kernel) - len(bank)
        self.assertEqual(kernel[:compressed_size], kernel2[:compressed_size])
        self.assertTrue(image)
        bank2 = kernel2[compressed_size:]
        trees = PATCH.split_dtb_bank(bank)
        self.assertEqual(len(trees), 68)
        changed = []
        for offset, length in trees:
            a, b = bank[offset:offset + length], bank2[offset:offset + length]
            if a != b:
                self.assertEqual(PATCH.fdt_model(a), PATCH.M3X_MODEL)
                pa, pb = properties(a), properties(b)
                self.assertEqual(pa.keys(), pb.keys())
                for key in pa:
                    if pa[key] != pb[key]:
                        self.assertIn("/tlmm_gpio_key/", key[0])
                        self.assertEqual(key[1], "pins")
                        expected = [pin for pin in pa[key].split(b"\0")
                                    if pin not in (b"gpio18", b"gpio19")]
                        self.assertEqual(b"\0".join(expected), pb[key])
                        self.assertIn(b"gpio13\0", pb[key])
                        self.assertIn(b"gpio17\0", pb[key])
                        changed.append(key)
        self.assertEqual(len(changed), 4)

    def test_rooted_patch_keeps_the_same_kernel_and_magisk_ramdisk(self):
        base = ROOT / "firmware/m3x_v1_75"
        stock = PATCH.parse_boot((base / "boot-dtbpatch.img").read_bytes())
        rooted = PATCH.parse_boot((base / "boot-magisk-dtb.img").read_bytes())
        magisk = PATCH.parse_boot((ROOT / "backups/magisk_patched-30700_25tcf.img").read_bytes())
        self.assertEqual(rooted[3], stock[3])
        self.assertEqual(rooted[4], magisk[4])
