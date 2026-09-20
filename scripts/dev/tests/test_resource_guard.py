"""Oracle-3 tests: resource_guard.py must refuse/serialize concurrent
writers of one build directory and clamp parallelism to the policy cap.

The concurrency tests launch real subprocesses of the tool; the payload is a
python one-liner that records its start/end monotonic timestamps, so the test
can prove ordering rather than assume it.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # dev_common lives in scripts/dev
sys.path.insert(0, str(_HERE))          # _fixtures lives beside this file

from _fixtures import run_tool  # noqa: E402

PAYLOAD = (
    "import sys, time, json, pathlib;"
    "tag = sys.argv[1];"
    "pathlib.Path(tag + '.started').write_text(str(time.monotonic()));"
    "time.sleep(float(sys.argv[2]));"
    "end = time.monotonic();"
    "pathlib.Path(tag + '.json').write_text(json.dumps({'start': float(pathlib.Path(tag + '.started').read_text()), 'end': end}))"
)


class ParallelismClampTest(unittest.TestCase):
    def test_cmake_parallel_clamped_to_two(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "seen.txt"
            probe = (
                "import sys, pathlib;"
                f"pathlib.Path(r'{out}').write_text(repr(sys.argv[1:]))"
            )
            build = Path(tmp) / "build-dev"
            build.mkdir()
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(build), "--json",
                             "--", sys.executable, "-c", probe,
                             "cmake", "--build", "build-dev", "--parallel", "8"],
                            cwd=Path(tmp))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            seen = out.read_text(encoding="utf-8")
            self.assertIn("'--parallel', '2'", seen)
            self.assertNotIn("'--parallel', '8'", seen)

    def test_ninja_j_flag_clamped_and_j1_pin_survives(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "seen.txt"
            probe = (
                "import sys, pathlib;"
                f"pathlib.Path(r'{out}').write_text(repr(sys.argv[1:]))"
            )
            build = Path(tmp) / "build-dev"
            build.mkdir()
            run_tool("resource_guard.py",
                     ["--lock-dir", str(build), "--", sys.executable, "-c", probe,
                      "ninja", "-j9", "-C", "build-dev"], cwd=Path(tmp))
            self.assertIn("'-j2'", out.read_text(encoding="utf-8"))
            run_tool("resource_guard.py",
                     ["--lock-dir", str(build), "--", sys.executable, "-c", probe,
                      "ninja", "-j1", "-C", "build-dev"], cwd=Path(tmp))
            # a deliberate -j1 pin is never raised
            self.assertIn("'-j1'", out.read_text(encoding="utf-8"))

    def test_governed_env_caps_parallelism(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "env.txt"
            probe = (
                "import os, pathlib;"
                f"pathlib.Path(r'{out}').write_text("
                "os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL','') + ',' + "
                "os.environ.get('CTEST_PARALLEL_LEVEL',''))"
            )
            build = Path(tmp) / "build-dev"
            build.mkdir()
            run_tool("resource_guard.py",
                     ["--lock-dir", str(build), "--", sys.executable, "-c", probe,
                      "cmake", "--build", "build-dev"], cwd=Path(tmp))
            self.assertEqual(out.read_text(encoding="utf-8"), "2,2")


class ConcurrencyTest(unittest.TestCase):
    def _launch(self, root: Path, tag: str, extra: list[str]) -> subprocess.Popen:
        argv = [sys.executable,
                str(Path(__file__).resolve().parents[1] / "resource_guard.py"),
                "--lock-dir", str(root / "build"), *extra,
                "--", sys.executable, "-c", PAYLOAD, str(root / tag), "0.6"]
        return subprocess.Popen(argv, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, cwd=str(root))

    @staticmethod
    def _wait(proc: subprocess.Popen) -> tuple[int, str]:
        """Wait, capture and close the pipes; return (exit code, stderr)."""
        rc = proc.wait(timeout=60)
        err = proc.stderr.read() if proc.stderr else ""
        out = proc.stdout.read() if proc.stdout else ""
        if proc.stderr:
            proc.stderr.close()
        if proc.stdout:
            proc.stdout.close()
        return rc, err + out

    @staticmethod
    def _wait_for(path: Path, budget: float = 30.0) -> None:
        """Wait for a payload-produced file; generous because python startup on
        a loaded host can take seconds (a fixed short budget made this test
        flaky under parallel agents)."""
        deadline = time.monotonic() + budget
        while time.monotonic() < deadline:
            if path.exists():
                return
            time.sleep(0.05)
        raise AssertionError(f"timed out waiting for {path}")

    def test_second_writer_refused_fail_fast(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            first = self._launch(root, "first", [])
            # the started-marker is written by the payload, i.e. strictly
            # after the guard acquired the lock — no polling race
            self._wait_for(root / "first.started")
            second = self._launch(root, "second", [])
            rc_second, err_second = self._wait(second)
            rc_first, err_first = self._wait(first)
            self.assertEqual(rc_first, 0, err_first)
            self.assertEqual(rc_second, 75, err_second)
            self.assertFalse((root / "second.started").exists(),
                             "refused payload still ran")
            self.assertTrue((root / "first.json").exists())
            self.assertFalse((root / "build.agent-build.lock").exists(),
                             "lock was not released")

    def test_wait_serializes_both_writers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            first = self._launch(root, "first", [])
            self._wait_for(root / "first.started")
            second = self._launch(root, "second", ["--wait", "60"])
            rc_first, err_first = self._wait(first)
            rc_second, err_second = self._wait(second)
            self.assertEqual(rc_first, 0, err_first)
            self.assertEqual(rc_second, 0, err_second)
            t_first = json.loads((root / "first.json").read_text())
            t_second = json.loads((root / "second.json").read_text())
            self.assertGreaterEqual(t_second["start"], t_first["end"],
                                    "second writer started before the first released")


class LockLifecycleTest(unittest.TestCase):
    def test_payload_failure_releases_lock_and_propagates_code(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(root / "build"),
                             "--", sys.executable, "-c", "import sys; sys.exit(3)"],
                            cwd=root)
            self.assertEqual(proc.returncode, 3, proc.stderr)
            self.assertFalse((root / "build.agent-build.lock").exists())

    def test_stale_dead_holder_lock_is_broken(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            lock = root / "build.agent-build.lock"
            holder = {"pid": 1 << 30, "host": "fixture",
                      "started_utc": "2020-01-01T00:00:00Z",
                      "started_monotonic": time.monotonic() - 7200.0,
                      "command": ["stale"]}
            lock.write_text(json.dumps(holder), encoding="utf-8")
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(root / "build"),
                             "--", sys.executable, "-c", "print('ran')"], cwd=root)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertFalse(lock.exists())

    def test_live_holder_lock_is_never_broken(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            lock = root / "build.agent-build.lock"
            holder = {"pid": os.getpid(), "host": "fixture",
                      "started_utc": "2020-01-01T00:00:00Z",
                      "started_monotonic": time.monotonic(),
                      "command": ["live"]}
            lock.write_text(json.dumps(holder), encoding="utf-8")
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(root / "build"),
                             "--", sys.executable, "-c", "print('ran')"], cwd=root)
            self.assertEqual(proc.returncode, 75, proc.stderr)
            self.assertTrue(lock.exists(), "live holder's lock was broken")

    def test_status_reports_free_and_held(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(root / "build"), "--status", "--json"],
                            cwd=root)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertFalse(json.loads(proc.stdout)["held"])
            (root / "build.agent-build.lock").write_text(
                json.dumps({"pid": os.getpid(), "host": "x",
                            "started_monotonic": time.monotonic()}), encoding="utf-8")
            proc = run_tool("resource_guard.py",
                            ["--lock-dir", str(root / "build"), "--status", "--json"],
                            cwd=root)
            data = json.loads(proc.stdout)
            self.assertTrue(data["held"])
            self.assertTrue(data["holder_alive"])


if __name__ == "__main__":
    unittest.main()
