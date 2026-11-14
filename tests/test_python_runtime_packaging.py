"""Integrity checks for the vendored runtime preparation seam (no downloads)."""
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_python_runtime", ROOT / "scripts/prepare_python_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


class PythonRuntimePackagingTests(unittest.TestCase):
    def test_locked_distributions_use_one_exact_python_release(self):
        lock = runtime.load_lock()
        self.assertEqual(lock["python_version"], "3.12.16")
        self.assertEqual(lock["release"], "20261110")
        for target in ("x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu",
                       "x86_64-apple-darwin", "aarch64-apple-darwin"):
            artifact = lock["distributions"][target]
            self.assertIn("cpython-3.12.16%2B20261110-", artifact["url"])
            self.assertEqual(len(artifact["sha256"]), 64)
            self.assertTrue(artifact["url"].endswith("-install_only_stripped.tar.gz"))

    def test_hash_mismatch_never_uses_a_corrupt_cached_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "python.tar.gz"
            archive.write_bytes(b"corrupt cached archive")
            with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                runtime.require_digest(archive, "0" * 64)

    def test_archive_cannot_write_outside_its_python_root(self):
        for name in ("python/../../escape", "../escape", "/absolute"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / "python.tar.gz"
                with tarfile.open(archive, "w:gz") as output:
                    member = tarfile.TarInfo(name)
                    member.size = 1
                    output.addfile(member, io.BytesIO(b"x"))
                with self.assertRaises((RuntimeError, tarfile.FilterError)):
                    runtime.extract_runtime(archive, Path(directory) / "unpacked")
                self.assertFalse((Path(directory).parent / "escape").exists())

    def test_archive_links_must_stay_inside_python(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "python.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                member = tarfile.TarInfo("python/bin/python3")
                member.type = tarfile.SYMTYPE
                member.linkname = "../../../outside"
                output.addfile(member)
            with self.assertRaises((RuntimeError, tarfile.FilterError)):
                runtime.extract_runtime(archive, Path(directory) / "unpacked")

    def test_inventory_detects_modified_missing_and_extra_files(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            payload = prefix / "payload"
            payload.write_bytes(b"locked bytes")
            entries = runtime.file_inventory(prefix)
            runtime.verify_inventory(prefix, entries)
            payload.write_bytes(b"changed bytes")
            with self.assertRaisesRegex(RuntimeError, "integrity"):
                runtime.verify_inventory(prefix, entries)
            payload.unlink()
            with self.assertRaisesRegex(RuntimeError, "inventory"):
                runtime.verify_inventory(prefix, entries)
            payload.write_bytes(b"locked bytes")
            (prefix / "unrecorded").write_bytes(b"extra")
            with self.assertRaisesRegex(RuntimeError, "inventory"):
                runtime.verify_inventory(prefix, entries)

    def test_requirement_hashes_are_required_for_every_cached_wheel(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            wheel = wheelhouse / "example-1.0-py3-none-any.whl"
            wheel.write_bytes(b"wheel bytes")
            lock = b"example==1.0 --hash=sha256:" + hashlib.sha256(b"wheel bytes").hexdigest().encode()
            runtime.verify_wheels(wheelhouse, lock)
            wheel.write_bytes(b"different wheel bytes")
            with self.assertRaisesRegex(RuntimeError, "wheel.*hash"):
                runtime.verify_wheels(wheelhouse, lock)

    def test_an_incomplete_wheelhouse_cannot_become_a_ready_package(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            wheel = wheelhouse / "example-1.0-py3-none-any.whl"
            wheel.write_bytes(b"wheel bytes")
            digest = hashlib.sha256(b"wheel bytes").hexdigest()
            lock = (f"example==1.0 --hash=sha256:{digest}\n"
                    f"missing==2.0 --hash=sha256:{digest}\n").encode()
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                runtime.verify_wheels(wheelhouse, lock)

    @unittest.skipIf(os.name == "nt", "POSIX executable permission contract")
    def test_inventory_detects_removed_executable_permissions(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            payload = prefix / "python"
            payload.write_bytes(b"executable bytes")
            payload.chmod(0o755)
            entries = runtime.file_inventory(prefix)
            payload.chmod(0o644)
            with self.assertRaisesRegex(RuntimeError, "integrity"):
                runtime.verify_inventory(prefix, entries)

    def test_valid_hash_does_not_admit_a_wheel_for_another_python_abi(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            wheel = wheelhouse / "example-1.0-cp999-cp999-win_amd64.whl"
            wheel.write_bytes(b"wheel bytes")
            digest = hashlib.sha256(b"wheel bytes").hexdigest()
            lock = f"example==1.0 --hash=sha256:{digest}".encode()
            with self.assertRaisesRegex(RuntimeError, "wheel.*compatible"):
                runtime.verify_wheels(wheelhouse, lock, sys.executable)


if __name__ == "__main__":
    unittest.main()
