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
                                     "--archive", str(archive)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(set(zipped.namelist()), {root.name + "/manifest.json", *(root.name + "/" + name for name in fixtures)})
                for name, content in fixtures.items():
                    self.assertEqual(zipped.read(root.name + "/" + name), content)


if __name__ == "__main__":
    unittest.main()
