"""The application archive must retain every receipted file, including dotfiles."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/archive_windows_application.py"


class WindowsApplicationArchiveTests(unittest.TestCase):
    def run_legacy_console_cli(self, script, *arguments):
        # -I ignores PYTHONIOENCODING. Exercise the actual public entry point
        # after forcing both streams to the English Windows console encoding.
        wrapper = (
            "import runpy, sys; "
            "sys.stdout.reconfigure(encoding='cp1252', errors='strict'); "
            "sys.stderr.reconfigure(encoding='cp1252', errors='strict'); "
            "sys.argv = sys.argv[1:]; "
            "runpy.run_path(sys.argv[0], run_name='__main__')"
        )
        return subprocess.run([sys.executable, "-I", "-B", "-c", wrapper, str(script), *map(str, arguments)],
                              capture_output=True, timeout=30)

    def test_public_cli_archives_chinese_paths_with_a_cp1252_console(self):
        with tempfile.TemporaryDirectory(prefix="RS CLI 中文 ") as temporary:
            root = Path(temporary) / "应用 目录"
            root.mkdir()
            (root / "manifest.json").write_text(json.dumps({
                "schema": "sicnu.windows-package-manifest/1", "files": []}), encoding="utf-8")
            archive = Path(temporary) / "应用 交付.zip"
            result = self.run_legacy_console_cli(SCRIPT, "--package-root", root, "--archive", archive)
            self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
            self.assertIn(str(archive), result.stdout.decode("utf-8"))
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(zipped.namelist(), [root.name + "/manifest.json"])

    @unittest.skipUnless(os.name == "nt", "Pi runtime has a Windows x64 distribution")
    def test_prepare_clis_report_missing_chinese_prefix_without_encoding_traceback(self):
        with tempfile.TemporaryDirectory(prefix="RS missing 中文 ") as temporary:
            missing = Path(temporary) / "不存在的 环境"
            for name, arguments, diagnostic in (
                ("prepare_pi_runtime.py", ["--verify-stage", missing], "Pi runtime preparation failed:"),
                ("prepare_pi_product.py", ["--verify-stage", missing, "--sdk-stage", missing], "Pi product preparation failed:"),
            ):
                with self.subTest(script=name):
                    result = self.run_legacy_console_cli(SCRIPT.with_name(name), *arguments)
                    self.assertEqual(result.returncode, 1)
                    error = result.stderr.decode("utf-8")
                    self.assertIn(diagnostic, error)
                    self.assertIn(missing.name, error)
                    self.assertNotIn("Traceback", error)
                    self.assertNotIn("UnicodeEncodeError", error)

    def test_failed_publication_removes_only_its_owned_partial_archive(self):
        spec = importlib.util.spec_from_file_location("windows_archive", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix="RS archive failure ") as temporary:
            root = Path(temporary) / "package"
            root.mkdir()
            (root / "manifest.json").write_text(json.dumps({
                "schema": "sicnu.windows-package-manifest/1", "files": []}), encoding="utf-8")
            archive = Path(temporary) / "application.zip"
            real_open = Path.open

            class FailingOutput:
                def __enter__(self):
                    self.file = real_open(archive, "xb")
                    return self

                def write(self, _block):
                    self.file.write(b"partial fixture ZIP")
                    raise OSError("Fixture: disk full")

                def __exit__(self, *_args):
                    self.file.close()

            def failing_open(path, *args, **kwargs):
                if path == archive and args == ("xb",):
                    return FailingOutput()
                return real_open(path, *args, **kwargs)

            with patch.object(Path, "open", failing_open), self.assertRaisesRegex(OSError, "disk full"):
                module.archive_package(root, archive)
            self.assertFalse(archive.exists(), "A failed publication must not leave a release ZIP")
            archive.write_bytes(b"existing release fixture")
            with self.assertRaisesRegex(RuntimeError, "fresh"):
                module.archive_package(root, archive)
            self.assertEqual(archive.read_bytes(), b"existing release fixture")

    def test_manifested_hidden_and_long_resources_survive_archive(self):
        with tempfile.TemporaryDirectory(prefix="RS archive 中文 ") as temporary:
            root = Path(temporary) / "app with spaces"
            entries = []
            fixtures = {"bin/app.exe": b"fixture executable",
                        "libexec/pi/.notice": b"retained dotfile",
                        "libexec/pi/" + "long segment/" * 18 + "data.wasm": b"fixture WASM"}
            for relative, content in fixtures.items():
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(content)
                if os.name == "nt" and relative.endswith(".notice"):
                    import ctypes
                    self.assertTrue(ctypes.windll.kernel32.SetFileAttributesW(str(target), 0x02))
                entries.append({"path": relative, "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()})
            (root / "manifest.json").write_text(json.dumps({
                "schema": "sicnu.windows-package-manifest/1", "source_commit": "fixture-only", "files": entries}), encoding="utf-8")
            archive = Path(temporary) / "application.zip"
            result = subprocess.run([sys.executable, "-I", "-B", str(SCRIPT), "--package-root", str(root),
                                     "--archive", str(archive)], capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(set(zipped.namelist()), {root.name + "/manifest.json", *(root.name + "/" + name for name in fixtures)})
                for name, content in fixtures.items():
                    self.assertEqual(zipped.read(root.name + "/" + name), content)


if __name__ == "__main__":
    unittest.main()
