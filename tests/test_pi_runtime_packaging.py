"""Vendored Pi preparation integrity/negative cases; no downloads or npm runs."""
import base64
from contextlib import redirect_stderr
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_pi_runtime", ROOT / "scripts/prepare_pi_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)


class PiRuntimePackagingTests(unittest.TestCase):
    def test_release_recipes_pin_one_coherent_sdk_and_node(self):
        pi = runtime.read_json(ROOT / "vendor/pi/runtime.lock.json")
        node = runtime.read_json(ROOT / "vendor/node/runtime.lock.json")
        package = runtime.read_json(ROOT / "vendor/pi/package.json")
        self.assertEqual(pi["pi_version"], "1.5.2")
        self.assertEqual(pi["source"]["commit"], "b4afdddf44e059c67ccf5097e5317d913521b470")
        self.assertEqual(set(package["dependencies"].values()), {pi["pi_version"]})
        self.assertEqual(node["node_version"], "24.22.3")
        artifact = node["distributions"]["x86_64-pc-windows-msvc"]
        self.assertEqual(artifact["sha256"], "e612af99f626ab415ea225af6bce5c971feb448cb5cae250bb1c3fa50c5f7d9f")
        self.assertNotIn("npm", artifact["runtime_files"])

    def test_unsafe_paths_are_rejected_before_filesystem_access(self):
        for value in ("../escape", "a/../../escape", "/absolute", "C:/absolute", "a\\..\\escape", "./file", ""):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, "Unsafe"):
                runtime.relative_path(value)

    def test_unsupported_host_does_not_fall_back_to_system_node(self):
        for system, machine in (("linux", "x86_64"), ("darwin", "arm64"), ("win32", "arm64")):
            with self.subTest(system=system, machine=machine), \
                 patch.object(runtime.sys, "platform", system), \
                 patch.object(runtime.platform, "machine", return_value=machine):
                with self.assertRaisesRegex(RuntimeError, "No vendored Pi/Node distribution"):
                    runtime.native_target()

    def test_node_zip_rejects_escape_wrong_root_symlink_and_case_collision(self):
        artifact = {"archive_root": "node-test", "runtime_files": []}
        for names in (("node-test/../../escape",), ("other/file",), ("node-test/A", "node-test/a")):
            with self.subTest(names=names), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive = root / "node.zip"
                with zipfile.ZipFile(archive, "w") as output:
                    for name in names:
                        output.writestr(name, b"x")
                with self.assertRaisesRegex(RuntimeError, "Unsafe|root|Duplicate"):
                    runtime.extract_node(archive, root / "unpacked", artifact)
                self.assertFalse((root / "escape").exists())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "node.zip"
            with zipfile.ZipFile(archive, "w") as output:
                link = zipfile.ZipInfo("node-test/node.exe")
                link.create_system = 3
                link.external_attr = 0o120777 << 16
                output.writestr(link, "../../outside")
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                runtime.extract_node(archive, root / "unpacked", artifact)

    def test_node_zip_must_contain_build_time_npm_without_shipping_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "node.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr("node-test/node.exe", b"fake node")
            with self.assertRaisesRegex(RuntimeError, "npm-cli"):
                runtime.extract_node(archive, root / "unpacked", {"archive_root": "node-test", "runtime_files": ["node.exe"]})

    def test_corrupt_cached_node_is_not_used_or_silently_refetched(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            (cache / "node.zip").write_bytes(b"corrupt")
            with patch.object(runtime, "urlopen") as network:
                with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                    runtime.fetch_artifact({"url": "https://nodejs.org/dist/node.zip", "sha256": "0" * 64}, cache, False)
                network.assert_not_called()

    def test_offline_cache_miss_never_attempts_a_download(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(runtime, "urlopen") as network:
            with self.assertRaisesRegex(RuntimeError, "Offline cache"):
                runtime.fetch_artifact({"url": "https://nodejs.org/dist/node.zip", "sha256": "0" * 64}, Path(temporary), True)
            network.assert_not_called()

    def test_complete_component_inventory_rejects_modified_missing_and_extra_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            payload = prefix / runtime.PI_ROOT / "node_modules/example/data.wasm"
            payload.parent.mkdir(parents=True)
            payload.write_bytes(b"locked WASM")
            entries = runtime.file_inventory(prefix)
            runtime.verify_inventory(prefix, entries)
            payload.write_bytes(b"changed WASM")
            with self.assertRaisesRegex(RuntimeError, "integrity"):
                runtime.verify_inventory(prefix, entries)
            payload.unlink()
            with self.assertRaisesRegex(RuntimeError, "inventory"):
                runtime.verify_inventory(prefix, entries)
            payload.write_bytes(b"locked WASM")
            payload.with_name("unrecorded.node").write_bytes(b"extra native resource")
            with self.assertRaisesRegex(RuntimeError, "inventory"):
                runtime.verify_inventory(prefix, entries)

    def test_payload_directory_links_refuse_before_external_traversal(self):
        for placement in ("prefix", "component-root", "nested"):
            with self.subTest(placement=placement), tempfile.TemporaryDirectory() as temporary:
                owned = Path(temporary).resolve()
                prefix = owned / "prefix"
                external = owned / "external"
                external.mkdir()
                marker = external / "must-remain.bin"
                marker.write_bytes(b"owned external marker")
                if placement == "prefix":
                    link = prefix
                elif placement == "component-root":
                    link = prefix / runtime.PI_ROOT
                else:
                    link = prefix / runtime.PI_ROOT / "node_modules/outside"
                link.parent.mkdir(parents=True, exist_ok=True)
                self.assertTrue(link.parent.resolve().is_relative_to(owned))
                self.assertTrue(external.is_relative_to(owned))
                self.assertFalse(external.is_relative_to(prefix))
                try:
                    if os.name == "nt":
                        environment = os.environ.copy()
                        environment["SICNU_TEST_JUNCTION_LINK"] = str(link)
                        environment["SICNU_TEST_JUNCTION_TARGET"] = str(external)
                        powershell = Path(os.environ["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
                        created = subprocess.run(
                            [str(powershell), "-NoProfile", "-NonInteractive", "-Command",
                             "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:SICNU_TEST_JUNCTION_LINK -Target $env:SICNU_TEST_JUNCTION_TARGET | Out-Null"],
                            env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=30,
                            creationflags=subprocess.CREATE_NO_WINDOW)
                        self.assertEqual(created.returncode, 0, created.stdout)
                    else:
                        link.symlink_to(external, target_is_directory=True)
                    self.assertTrue(link.exists())
                    real_scandir = os.scandir

                    def no_external_scan(directory):
                        self.assertFalse(Path(directory).resolve().is_relative_to(external),
                                         "Inventory followed the payload link outside its prefix")
                        return real_scandir(directory)

                    with patch.object(runtime.os, "scandir", side_effect=no_external_scan):
                        with self.assertRaisesRegex(RuntimeError, "link|reparse"):
                            runtime.file_inventory(prefix)
                        diagnostic = io.StringIO()
                        with patch.object(runtime.sys, "argv", ["prepare_pi_runtime.py", "--verify-stage", str(prefix)]), \
                             redirect_stderr(diagnostic), self.assertRaises(SystemExit) as refused:
                            runtime.main()
                        self.assertEqual(refused.exception.code, 1)
                        self.assertIn("reparse", diagnostic.getvalue())
                    self.assertEqual(marker.read_bytes(), b"owned external marker")
                finally:
                    # Never recursively remove a junction. Remove only the owned
                    # link and prove its separately owned target is still intact.
                    if link.exists() or link.is_symlink():
                        if os.name == "nt":
                            os.rmdir(link)
                        else:
                            link.unlink()
                    self.assertEqual(marker.read_bytes(), b"owned external marker")

    def test_verify_stage_rejects_other_targets_versions_and_recipe_identity(self):
        pi = {"pi_version": "1.5.2"}
        artifact = {"url": "https://nodejs.org/dist/node.zip", "sha256": "1" * 64}
        node = {"node_version": "24.22.3", "distributions": {"x86_64-pc-windows-msvc": artifact}}
        original = {"schema": "sicnu.pi-runtime/1", "target": "x86_64-pc-windows-msvc", "pi_version": "1.5.2",
                    "node_version": "24.22.3", "node_source": artifact, "recipes": {"source": "2" * 64}}
        for changes in ({"target": "aarch64-apple-darwin"}, {"pi_version": "9.9.9"},
                        {"node_version": "0.0.0"}, {"recipes": {}}, {"node_source": {}}):
            with self.subTest(changes=changes), tempfile.TemporaryDirectory() as temporary:
                prefix = Path(temporary)
                runtime.write_json(prefix / runtime.METADATA, original | changes)
                with patch.object(runtime, "load_locks", return_value=(pi, node, {}, {})), \
                     patch.object(runtime, "native_target", return_value="x86_64-pc-windows-msvc"), \
                     patch.object(runtime, "recipe_hashes", return_value=original["recipes"]):
                    with self.assertRaisesRegex(RuntimeError, "pinned native recipe"):
                        runtime.verify_stage(prefix)
    def test_npm_lock_requires_exact_release_integrity_and_registry_sources(self):
        package = {"dependencies": {"@earendil-works/pi-coding-agent": "1.5.2"}}
        integrity = "sha512-" + base64.b64encode(hashlib.sha512(b"fixture bytes").digest()).decode("ascii")
        pi = {"pi_version": "1.5.2", "package": "@earendil-works/pi-coding-agent", "package_integrity": integrity}
        valid = {"lockfileVersion": 3, "packages": {"": {"dependencies": package["dependencies"]},
                 "node_modules/@earendil-works/pi-coding-agent": {"version": "1.5.2", "resolved": "https://registry.npmjs.org/pi/-/pi.tgz", "integrity": integrity}}}
        runtime.validate_npm_lock(valid, package, pi)
        for change, message in (({"version": "1.5.3"}, "exactly pinned"),
                                ({"integrity": ""}, "integrity"), ({"integrity": None}, "integrity"),
                                ({"integrity": "sha512-YWJj"}, "integrity"),
                                ({"resolved": "http://registry.npmjs.org/pi.tgz"}, "registry"),
                                ({"resolved": "https://example.invalid/pi.tgz"}, "registry"),
                                ({"dev": True}, "non-production"), ({"link": True}, "non-production")):
            broken = json.loads(json.dumps(valid))
            broken["packages"]["node_modules/@earendil-works/pi-coding-agent"].update(change)
            with self.subTest(change=change), self.assertRaisesRegex(RuntimeError, message):
                runtime.validate_npm_lock(broken, package, pi)

    def test_patch_is_applied_only_to_exact_reviewed_original_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "sdk"
            target = sdk / "dist/runtime.js"
            target.parent.mkdir(parents=True)
            original, modified = b"const options = {};\n", b"const options = {authContext};\n"
            target.write_bytes(original)
            pi = {"pi_version": "1.5.2", "source": {"commit": "a" * 40}}
            recipe = {"schema": "sicnu.pi-vendor-patches/1", "upstream": {"version": "1.5.2", "commit": "a" * 40},
                      "patches": [{"id": "auth-context", "files": [{"path": "dist/runtime.js", "before_sha256": hashlib.sha256(original).hexdigest(),
                                  "after_sha256": hashlib.sha256(modified).hexdigest(), "replacements": [{"before": original.decode(), "after": modified.decode(), "count": 1}]}]}]}
            manifest = root / "manifest.json"
            runtime.write_json(manifest, recipe)
            runtime.apply_patches(sdk, manifest, pi)
            self.assertEqual(target.read_bytes(), modified)
            with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                runtime.apply_patches(sdk, manifest, pi)

    def test_patch_rejects_bad_output_digest_without_modifying_original(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "sdk/runtime.js"
            target.parent.mkdir()
            target.write_bytes(b"original")
            pi = {"pi_version": "1.5.2", "source": {"commit": "a" * 40}}
            recipe = {"schema": "sicnu.pi-vendor-patches/1", "upstream": {"version": "1.5.2", "commit": "a" * 40}, "patches": [{"id": "auth", "files": [
                       {"path": "runtime.js", "before_sha256": hashlib.sha256(b"original").hexdigest(), "after_sha256": "0" * 64,
                        "replacements": [{"before": "original", "after": "modified", "count": 1}]}]}]}
            manifest = root / "manifest.json"
            runtime.write_json(manifest, recipe)
            with self.assertRaisesRegex(RuntimeError, "digest differs"):
                runtime.apply_patches(target.parent, manifest, pi)
            self.assertEqual(target.read_bytes(), b"original")

    def test_preparation_environment_does_not_inherit_node_npm_or_credentials(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {
                "NODE_OPTIONS": "--import evil.mjs", "NODE_PATH": "outside", "NPM_CONFIG_USERCONFIG": "outside.npmrc",
                "AWS_SECRET_ACCESS_KEY": "fake-fixture-only", "GOOGLE_APPLICATION_CREDENTIALS": "outside.json", "OPENAI_API_KEY": "fake-fixture-only"}):
            environment = runtime.process_environment(Path(temporary) / "node", Path(temporary) / "state")
            for key in ("NODE_OPTIONS", "NODE_PATH", "NPM_CONFIG_USERCONFIG", "AWS_SECRET_ACCESS_KEY", "GOOGLE_APPLICATION_CREDENTIALS", "OPENAI_API_KEY"):
                self.assertNotIn(key, environment)
            self.assertEqual(environment["PI_TELEMETRY"], "0")
            self.assertTrue(Path(environment["USERPROFILE"]).is_relative_to(Path(temporary)))


if __name__ == "__main__":
    unittest.main()
