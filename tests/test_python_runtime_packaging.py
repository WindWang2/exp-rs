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

    def test_conditional_pins_require_an_explicit_target_interpreter(self):
        lock = ('example==1.0; sys_platform == "win32" --hash=sha256:' + '0' * 64).encode()
        with self.assertRaisesRegex(RuntimeError, "target interpreter"):
            runtime.locked_requirements(lock)

    def test_only_target_active_pins_require_wheels(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            (wheelhouse / "example-1.0-py3-none-any.whl").write_bytes(b"active wheel")
            active_hash = hashlib.sha256(b"active wheel").hexdigest()
            inactive_hash = hashlib.sha256(b"inactive wheel").hexdigest()
            lock = (f'example==1.0; sys_platform == "{sys.platform}" --hash=sha256:{active_hash}\n'
                    f'example==2.0; sys_platform == "unsupported-test-platform" --hash=sha256:{inactive_hash}\n').encode()
            runtime.verify_wheels(wheelhouse, lock, sys.executable)
            (wheelhouse / "example-2.0-py3-none-any.whl").write_bytes(b"inactive wheel")
            with self.assertRaisesRegex(RuntimeError, "wheel.*hash"):
                runtime.verify_wheels(wheelhouse, lock, sys.executable)

    def test_an_active_conditional_pin_cannot_be_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            (wheelhouse / "example-1.0-py3-none-any.whl").write_bytes(b"wheel")
            digest = hashlib.sha256(b"wheel").hexdigest()
            lock = (f'example==1.0 --hash=sha256:{digest}\n'
                    f'missing==2.0; python_version >= "3.0" --hash=sha256:{digest}\n').encode()
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                runtime.verify_wheels(wheelhouse, lock, sys.executable)

    def test_multiple_active_pins_for_one_package_are_rejected(self):
        digest = hashlib.sha256(b"wheel").hexdigest()
        for interpreter in (None, sys.executable):
            with self.subTest(interpreter=interpreter):
                lock = (f'example==1.0 --hash=sha256:{digest}\n'
                        f'EXAMPLE==1.0 --hash=sha256:{digest}\n').encode()
                with self.assertRaisesRegex(RuntimeError, "multiple active pins"):
                    runtime.locked_requirements(lock, interpreter)

    def test_exact_pins_reject_urls_extras_ranges_and_wildcards(self):
        for requirement in ('example>=1.0', 'example==1.*', 'example[extra]==1.0',
                            'example @ https://example.invalid/example-1.0-py3-none-any.whl',
                            'example==1.0,!=1.0.1'):
            for interpreter in (None, sys.executable):
                with self.subTest(requirement=requirement, interpreter=interpreter):
                    lock = (requirement + ' --hash=sha256:' + '0' * 64).encode()
                    with self.assertRaisesRegex(RuntimeError, "exact package pins"):
                        runtime.locked_requirements(lock, interpreter)

    def test_inactive_pins_still_require_exact_versions_and_hashes(self):
        for lock, message in (
                (b'example>=1.0; sys_platform == "unsupported-test-platform" --hash=sha256:' + b'0' * 64,
                 "exact package pins"),
                (b'example==1.0; sys_platform == "unsupported-test-platform"', "package hashes")):
            with self.subTest(lock=lock), self.assertRaisesRegex(RuntimeError, message):
                runtime.locked_requirements(lock, sys.executable)

    def test_target_packaging_normalizes_wheel_names_and_versions(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            (wheelhouse / "example_name-1.0-py3-none-any.whl").write_bytes(b"wheel")
            digest = hashlib.sha256(b"wheel").hexdigest()
            lock = f'Example.Name==1.0.0 --hash=sha256:{digest}'.encode()
            runtime.verify_wheels(wheelhouse, lock, sys.executable)

    def test_duplicate_compatible_wheels_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            wheelhouse = Path(directory)
            (wheelhouse / "example-1.0-py3-none-any.whl").write_bytes(b"wheel")
            (wheelhouse / "example-1.0-1-py3-none-any.whl").write_bytes(b"wheel")
            digest = hashlib.sha256(b"wheel").hexdigest()
            lock = f'example==1.0 --hash=sha256:{digest}'.encode()
            with self.assertRaisesRegex(RuntimeError, "wheel.*hash"):
                runtime.verify_wheels(wheelhouse, lock, sys.executable)

    def test_hash_cannot_be_reused_by_an_unlocked_package_or_version(self):
        for filename in ("other-1.0-py3-none-any.whl", "example-2.0-py3-none-any.whl"):
            with self.subTest(filename=filename), tempfile.TemporaryDirectory() as directory:
                wheelhouse = Path(directory)
                (wheelhouse / filename).write_bytes(b"wheel")
                digest = hashlib.sha256(b"wheel").hexdigest()
                lock = f'example==1.0 --hash=sha256:{digest}'.encode()
                with self.assertRaisesRegex(RuntimeError, "wheel.*hash"):
                    runtime.verify_wheels(wheelhouse, lock, sys.executable)

    def test_large_universal_hash_lock_does_not_use_command_line_payloads(self):
        # Universal binary locks can exceed Windows' CreateProcess limit.
        hashes = ' '.join('--hash=sha256:' + f'{value:064x}' for value in range(500))
        lock = f'example==1.0; python_version >= "3.0" {hashes}'.encode()
        locked = runtime.locked_requirements(lock, sys.executable)
        self.assertEqual(len(locked[("example", "1")]), 500)

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
