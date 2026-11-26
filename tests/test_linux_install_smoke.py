"""Fast shell/workflow contracts; fixture SDK files are NOT runtime evidence.

The real installed CLI smoke is a separate CI command using the actual Qt SDK.
"""
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/verify_linux_install.sh"


class LinuxInstallSmokeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="install smoke ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.prefix = self.root / "installed prefix"
        self.qt = self.root / "Qt SDK"
        (self.prefix / "bin").mkdir(parents=True)
        (self.qt / "lib").mkdir(parents=True)
        for name in ("libQt6Core.so.6", "libQt6UiTools.so.6"):
            (self.qt / "lib" / name).touch()
        self.write_executable("sicnu_worker", "exit 0")
        self.write_executable("sicnu_geo_rs_cli", 'test "$1" = --help; printf "%s\n" "$LD_LIBRARY_PATH"')

    def write_executable(self, name, body):
        path = self.prefix / "bin" / name
        path.write_text("#!/bin/sh\nset -eu\n" + body + "\n")
        path.chmod(0o755)

    def run_smoke(self, qt=None):
        env = dict(os.environ, LD_LIBRARY_PATH="/poison/build/lib")
        return subprocess.run(["bash", str(SCRIPT), str(self.prefix), str(self.qt) if qt is None else qt],
                              env=env, capture_output=True, text=True, check=False)

    def test_only_external_qt_is_supplied_even_with_spaces(self):
        result = self.run_smoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), str((self.qt / "lib").resolve()))
        self.assertNotIn("/poison/build", result.stdout)

    def test_empty_action_output_is_rejected_not_converted_to_lib(self):
        result = self.run_smoke("")
        self.assertEqual(result.returncode, 2)
        self.assertIn("non-empty-qt-sdk-prefix", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_missing_uitools_is_not_silently_ignored(self):
        (self.qt / "lib/libQt6UiTools.so.6").unlink()
        result = self.run_smoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing external Qt SDK runtime dependency", result.stderr)
        self.assertIn("libQt6UiTools.so.6", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_worker_is_still_required_in_the_install(self):
        (self.prefix / "bin/sicnu_worker").unlink()
        result = self.run_smoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing installed executable", result.stderr)

    def test_cli_failure_is_propagated(self):
        self.write_executable("sicnu_geo_rs_cli", "exit 37")
        self.assertEqual(self.run_smoke().returncode, 37)

    def test_workflow_uses_the_pinned_actions_real_output(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        outputs = set(re.findall(r"steps\.qt\.outputs\.([a-z0-9-]+)", workflow))
        self.assertTrue(outputs)
        self.assertLessEqual(outputs, {"host", "target", "version", "arch", "qt-root-dir",
                                      "qt-plugin-path", "qml2-import-path", "qt-host-path"})
        self.assertNotIn("steps.qt.outputs.qt-path", workflow)
        self.assertIn('bash scripts/verify_linux_install.sh /tmp/sicnu_install_test "${{ steps.qt.outputs.qt-root-dir }}"', workflow)
        self.assertLess(workflow.index("python3 tests/test_linux_install_smoke.py"),
                        workflow.index("- name: Compile Core & Test Targets"))


if __name__ == "__main__":
    unittest.main()
