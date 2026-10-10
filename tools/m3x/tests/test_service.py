"""Exercise launcher exits with fake sysfs and harmless child processes."""
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]


class ServiceTests(unittest.TestCase):
    def run_service(self, cpu="35", battery="25000", disabled=False, later=None,
                    runtime_state="stopped", display_state="stopped", child_exit=0,
                    already_running=False):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            mod = base / "module"
            mod.mkdir()
            commands = base / "bin"
            commands.mkdir()
            sysfs = base / "sys"
            for number, (name, value) in enumerate((("tsens_tz_sensor0", cpu), ("battery", battery))):
                zone = sysfs / "class/thermal" / f"thermal_zone{number}"
                zone.mkdir(parents=True)
                (zone / "type").write_text(name)
                (zone / "temp").write_text(value)
            light = sysfs / "class/leds/lcd-backlight"
            light.mkdir(parents=True)
            (light / "brightness").write_text("123")
            (sysfs / "power").mkdir()
            (sysfs / "power/wake_unlock").touch()
            log = base / "calls"

            def executable(path, contents):
                path.write_text("#!/bin/sh\n" + contents)
                path.chmod(0o755)

            for command in ("start", "stop"):
                executable(commands / command, f'echo {command} "$@" >> "{log}"\n')
            if already_running == "later":
                pidof_script = (f'if [ "$1" = rockbox ]; then\n'
                                f'  if [ -e "{base}/pidof.checked" ]; then echo 1234; exit 0; fi\n'
                                f'  touch "{base}/pidof.checked"\nfi\nexit 1\n')
            elif already_running:
                pidof_script = 'case "$1" in rockbox) echo 1234; exit 0;; esac\nexit 1\n'
            else:
                pidof_script = "exit 1\n"
            executable(commands / "pidof", pidof_script)
            executable(commands / "getprop", f'case "$1" in init.svc.surfaceflinger) echo {display_state};; *) echo {runtime_state};; esac\n')
            # Accelerate launcher waits while keeping the real monitor/child race.
            executable(commands / "sleep", '/bin/sleep 0.02\n')
            # Leave ample time for the monitor under parallel compiler load.
            lifetime = 3 if later else 0.25
            executable(mod / "rockbox", f'echo launch >> "{log}"\n/bin/sleep {lifetime}\nexit {child_exit}\n')
            executable(base / "fbpan", f'echo fbpan "$@" >> "{log}"\n')
            source = (ROOT / "m3x-module/service.sh").read_text()
            source = source.replace("/sys/", f"{sysfs}/")
            source = source.replace("/data/local/tmp/rockbox-m3x.log", str(base / "service.log"))
            source = source.replace("/data/local/tmp/fbpan", str(base / "fbpan"))
            (mod / "service.sh").write_text(source)
            if disabled:
                (mod / "disable").touch()
            timer = None
            if later:
                def update():
                    deadline = time.monotonic() + 8
                    ready_log = base / "service.log" if later == "cool" else log
                    ready_text = "waiting for device to cool" if later == "cool" else "launch"
                    while not ready_log.exists() or ready_text not in ready_log.read_text():
                        if time.monotonic() >= deadline:
                            return
                        time.sleep(0.005)
                    time.sleep(0.03)
                    if later == "disable":
                        (mod / "disable").touch()
                    else:
                        temperature = sysfs / "class/thermal/thermal_zone0/temp"
                        pending = temperature.with_suffix(".pending")
                        pending.write_text("35" if later == "cool" else "55")
                        if later == "cool":
                            with log.open("a") as stream:
                                stream.write("cooled\n")
                        pending.replace(temperature)
                timer = threading.Thread(target=update)
                timer.start()
            env = dict(os.environ, PATH=f"{commands}:{os.environ['PATH']}")
            subprocess.run(["/bin/sh", str(mod / "service.sh")], env=env,
                           capture_output=True, timeout=10)
            if timer:
                timer.join()
            return (log.read_text() if log.exists() else "",
                    (base / "service.log").read_text() if (base / "service.log").exists() else "",
                    (mod / "disable").exists(),
                    (light / "brightness").read_text().strip(),
                    (sysfs / "power/wake_unlock").read_text().strip())

    def test_disabled_does_nothing(self):
        calls, log, disabled, _, _ = self.run_service(disabled=True)
        self.assertEqual((calls, log, disabled), ("", "", True))

    def test_second_launch_leaves_existing_session_untouched(self):
        for existing_session in (True, "later"):
            with self.subTest(existing_session=existing_session):
                calls, log, disabled, brightness, unlock = self.run_service(already_running=existing_session)
                self.assertEqual(calls, "")
                self.assertIn("Rockbox already running", log)
                self.assertFalse(disabled)
                self.assertEqual((brightness, unlock), ("123", ""))

    def test_display_stop_failure_refuses_to_launch(self):
        calls, log, disabled, brightness, unlock = self.run_service(display_state="running")
        self.assertNotIn("launch", calls)
        self.assertIn("SurfaceFlinger did not stop", log)
        self.assertTrue(disabled)
        self.assertEqual((brightness, unlock), ("123", "rockbox"))
        self.assertIn("start surfaceflinger", calls)

    def test_crash_status_restores_android_without_respawn(self):
        calls, log, disabled, brightness, unlock = self.run_service(child_exit=139)
        self.assertEqual(calls.count("launch"), 1)
        self.assertIn("rockbox exited (139)", log)
        self.assertIn("start zygote_secondary", calls)
        self.assertTrue(disabled)
        self.assertEqual((brightness, unlock), ("123", "rockbox"))

    def test_exit_does_not_respawn(self):
        calls, log, disabled, brightness, unlock = self.run_service()
        self.assertEqual(calls.count("launch"), 1)
        self.assertIn("fbpan --pan 0", calls)
        self.assertIn("start surfaceflinger", calls)
        self.assertLess(calls.index("stop zygote"), calls.index("stop surfaceflinger"))
        self.assertLess(calls.index("start surfaceflinger"), calls.index("start zygote"))
        self.assertTrue(disabled)
        self.assertEqual((brightness, unlock), ("123", "rockbox"))

    def test_hot_or_missing_sensor_prevents_start(self):
        for cpu, battery in (("55", "25000"), ("35", "40000"), ("", "25000"), ("bad", "25000")):
            with self.subTest(cpu=cpu, battery=battery):
                calls, _, disabled, _, _ = self.run_service(cpu, battery)
                self.assertNotIn("launch", calls)
                self.assertTrue(disabled)
                self.assertIn("start surfaceflinger", calls)

    def test_monitor_stops_on_heat_or_disable(self):
        for later in ("heat", "disable"):
            with self.subTest(later=later):
                calls, log, disabled, _, _ = self.run_service(later=later)
                self.assertEqual(calls.count("launch"), 1)
                self.assertIn("start surfaceflinger", calls)
                self.assertTrue(disabled)
                if later == "heat":
                    self.assertIn("thermal stop", log)

    def test_hot_boot_waits_before_framework_takeover(self):
        calls, log, disabled, _, _ = self.run_service(cpu="55", later="cool")
        self.assertIn("waiting for device to cool", log)
        self.assertEqual(calls.count("launch"), 1)
        self.assertLess(calls.index("cooled"), calls.index("stop zygote"))
        self.assertTrue(disabled)

    def test_failed_framework_stop_refuses_takeover(self):
        calls, log, disabled, _, _ = self.run_service(runtime_state="running")
        self.assertNotIn("launch", calls)
        self.assertNotIn("stop surfaceflinger", calls)
        self.assertIn("framework did not stop", log)
        self.assertTrue(disabled)


if __name__ == "__main__":
    unittest.main()
