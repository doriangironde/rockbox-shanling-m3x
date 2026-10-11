"""Run the actual target drivers on the host with pipes and fake hardware."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TESTS = ROOT / "tools/m3x/tests"
IBASSO = ROOT / "rockbox/firmware/target/hosted/ibasso"


class NativeDriverTests(unittest.TestCase):
    def compile_and_run(self, source, extras=(), extra_flags=()):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "test"
            subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-DM3X_OFFLINE_TEST",
                            "-I" + str(TESTS / "host-include"), "-I" + str(TESTS / "include"),
                            "-I" + str(IBASSO), "-I" + str(ROOT / "rockbox/firmware/target/hosted"),
                            "-I" + str(IBASSO / "m3x"),
                            "-I" + str(ROOT / "rockbox/firmware/target/hosted/tinyalsa/include"),
                            "-I" + str(ROOT / "rockbox/lib/fixedpoint"),
                            *extra_flags, str(TESTS / source), *map(str, extras), "-o", str(output)], check=True)
            subprocess.run([str(output), str(Path(directory) / "sysfs")], check=True, timeout=10)

    def test_input_and_battery_replay(self):
        self.compile_and_run("input-battery.c", [IBASSO / "m3x/button-m3x.c",
                                               IBASSO / "powermgmt-ibasso.c", IBASSO / "sysfs-ibasso.c"])

    def test_backlight_remembers_level_and_retries(self):
        self.compile_and_run("backlight.c", [IBASSO / "sysfs-ibasso.c"])

    def test_android_return_exits_without_hardware_poweroff(self):
        self.compile_and_run("power-return.c")

    def test_audio_volume_mute_filters_and_mixer_lifetime(self):
        self.compile_and_run("audio-hardware.c")

    def test_sysfs_rejects_malformed_numbers(self):
        self.compile_and_run("sysfs.c", [ROOT / "rockbox/firmware/target/hosted/sysfs.c"])

    def test_native_bitmap_pixels_and_scalar_colors_agree(self):
        export = ROOT / "rockbox/firmware/export"
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / "autoconf.h").write_text(
                "#define SHANLING_M3X\n#define SIMULATOR\n"
                "#define APPLICATION\n#define __PCTOOL__\n")
            # Two 24-bit source pixels: red and blue, padded to a BMP row.
            pixels = bytes((0, 0, 255, 255, 0, 0, 0, 0))
            bitmap = directory / "colors.bmp"
            bitmap.write_bytes(
                struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54) +
                struct.pack("<IiiHHIIiiII", 40, 2, 1, 1, 24, 0, len(pixels), 0, 0, 0, 0) +
                pixels)
            converter = directory / "bmp2rb"
            subprocess.run(["cc", '-DAPPLICATION_NAME="bmp2rb"',
                            str(ROOT / "rockbox/tools/bmp2rb.c"), "-o", str(converter)], check=True)
            palette = subprocess.check_output(
                [str(converter), "-f", "10", "-i", "probe", str(bitmap)], text=True)
            source = directory / "colors.c"
            for pixel_format in ("RGBA8888", "XRGB8888"):
                with self.subTest(pixel_format=pixel_format):
                    source.write_text(
                        '#include <assert.h>\n#include "config.h"\n'
                        '#undef LCD_PIXELFORMAT\n'
                        f'#define LCD_PIXELFORMAT {pixel_format}\n'
                        '#include "lcd.h"\n' + palette +
                        'int main(void) {\n'
                        '  assert(sizeof(probe) == 2 * sizeof(fb_data));\n'
                        '  assert(FB_UNPACK_SCALAR_LCD(probe[0]) == LCD_RGBPACK(255, 0, 0));\n'
                        '  assert(FB_UNPACK_SCALAR_LCD(probe[1]) == LCD_RGBPACK(0, 0, 255));\n'
                        '  fb_data p = FB_SCALARPACK(LCD_RGBPACK(17, 83, 201));\n'
                        '  assert(p.r == 17 && p.g == 83 && p.b == 201);\n'
                        '  assert(FB_UNPACK_SCALAR_LCD(p) == LCD_RGBPACK(17, 83, 201));\n'
                        '  return 0;\n}\n')
                    output = directory / "colors"
                    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                    "-I" + str(directory), "-I" + str(export),
                                    str(source), "-o", str(output)], check=True)
                    subprocess.run([str(output)], check=True, timeout=10)

    def test_pcm_sample_width_channel_order_mute_and_frame_size(self):
        # The shared math library has existing Clang warnings on 64-bit hosts.
        self.compile_and_run("pcm-volume.c", [ROOT / "rockbox/lib/fixedpoint/fixedpoint.c"],
                             ["-Wno-error=constant-conversion", "-Wno-error=absolute-value"])

    def test_pcm_rate_change_waits_for_write_and_nested_callbacks(self):
        self.compile_and_run("pcm-thread.c")

    def test_pcm_rejects_failed_prepare_before_starting_worker(self):
        self.compile_and_run("pcm-thread.c", extra_flags=["-DM3X_TEST_PREPARE_FAILURE"])

    def test_pcm_write_error_exits_instead_of_retrying_dead_handle(self):
        self.compile_and_run("pcm-thread.c", extra_flags=["-DM3X_TEST_WRITE_FAILURE"])

    def test_pcm_continuous_writer_does_not_starve_controls(self):
        self.compile_and_run("pcm-thread.c", extra_flags=["-DM3X_TEST_CONTROL_LATENCY"])

    def test_pcm_hotplug_live_paused_repeated_busy_and_write_serialization(self):
        self.compile_and_run("pcm-hotplug.c")

    def test_earpods_card_discovery_caps_and_busy_handover(self):
        self.compile_and_run("pcm-usb.c")

    def test_earpods_enumeration_gap_at_startup_does_not_select_internal(self):
        self.compile_and_run("pcm-usb.c", extra_flags=["-DM3X_TEST_USB_ENUM_DELAY"])

    def test_other_usb_device_keeps_internal_dac(self):
        self.compile_and_run("pcm-usb.c", extra_flags=["-DM3X_TEST_OTHER_USB"])

    def test_earpods_busy_failure_recovers_to_internal(self):
        self.compile_and_run("pcm-usb.c", extra_flags=["-DM3X_TEST_USB_BUSY"])

    def test_earpods_removed_during_handover_recovers_to_internal(self):
        self.compile_and_run("pcm-usb.c", extra_flags=["-DM3X_TEST_USB_REMOVAL"])
