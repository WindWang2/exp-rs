"""Small product-wrapper packaging contracts; no Node, npm, network or SDK scan."""
from contextlib import ExitStack, redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_pi_product", ROOT / "scripts/prepare_pi_product.py")
product = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(product)
runtime = product.runtime
TARGET = "x86_64-pc-windows-msvc"


class PiProductPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.owned = Path(self.temporary.name).resolve()
        self.source = self.owned / "source"
        self.source.mkdir()
        for name in product.PRODUCT_FILES:
            (self.source / name).write_text("// fixture: " + name + "\n", encoding="utf-8")
        self.sdk = self.owned / "sdk"
        pi, node, _, _ = runtime.load_locks()
        self.sdk_metadata = {"schema": "sicnu.pi-runtime/1", "target": TARGET,
                             "pi_version": pi["pi_version"], "node_version": node["node_version"],
                             "node_source": node["distributions"][TARGET], "recipes": runtime.recipe_hashes(pi)}
        runtime.write_json(self.sdk / runtime.METADATA, self.sdk_metadata)
        self.destination = self.owned / "product"
        self.patches = ExitStack()
        self.addCleanup(self.patches.close)
        self.patches.enter_context(patch.object(product, "SOURCE", self.source))
        self.patches.enter_context(patch.object(runtime, "native_target", return_value=TARGET))

    def prepare(self):
        with redirect_stdout(io.StringIO()):
            product.prepare(self.destination, self.sdk)
        return product.verify_stage(self.destination, self.sdk)

    def test_prepare_is_small_atomic_and_binds_raw_sdk_receipt(self):
        original = (self.sdk / runtime.METADATA).read_bytes()
        inventory = runtime.file_inventory

        def wrapper_inventory(prefix, roots):
            self.assertNotEqual(prefix, self.sdk)
            self.assertEqual(tuple(roots), (product.PRODUCT_ROOT,))
            return inventory(prefix, roots)

        with patch.object(runtime, "verify_stage", side_effect=AssertionError("full SDK scan")), \
             patch.object(runtime, "file_inventory", side_effect=wrapper_inventory), \
             patch.object(runtime, "checked_run", side_effect=AssertionError("process")), \
             patch.object(runtime.subprocess, "run", side_effect=AssertionError("process")), \
             patch.object(runtime, "urlopen", side_effect=AssertionError("network")):
            receipt = self.prepare()
        self.assertEqual((self.sdk / runtime.METADATA).read_bytes(), original)
        self.assertEqual(receipt["schema"], "sicnu.pi-product/1")
        self.assertEqual(receipt["protocol_version"], 1)
        self.assertEqual(receipt["entry"], (product.PRODUCT_ROOT / "main.mjs").as_posix())
        self.assertEqual(receipt["requires_sdk"]["receipt_sha256"], hashlib.sha256(original).hexdigest())
        expected = json.dumps(self.sdk_metadata["recipes"], sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        self.assertEqual(receipt["requires_sdk"]["recipe_digest"], hashlib.sha256(expected.encode("utf-8")).hexdigest())
        self.assertEqual(len(receipt["files"]), 3)
        self.assertEqual({entry["path"] for entry in receipt["sources"]},
                         {"pi/product-runtime/" + name for name in product.PRODUCT_FILES})
        self.assertEqual({entry.name for entry in self.destination.iterdir()}, {"libexec", "share"})
        self.assertEqual(list(self.owned.glob("product-prepare-*")), [])

    def test_recipe_digest_is_platform_independent_and_canonical(self):
        with patch.object(runtime, "native_target", side_effect=AssertionError("native target")), \
             patch.object(product, "sdk_identity", side_effect=AssertionError("SDK access")):
            self.assertEqual(product.expected_recipe_digest(), product.recipe_digest(self.sdk_metadata["recipes"]))
        self.assertEqual(product.recipe_digest({"b": "2", "a": "1"}),
                         hashlib.sha256(b'{"a":"1","b":"2"}').hexdigest())

    def test_existing_destination_is_verified_and_never_overwritten(self):
        self.prepare()
        receipt = (self.destination / product.METADATA).read_bytes()
        (self.source / "main.mjs").write_text("// changed source\n", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "source|recipe"):
            product.prepare(self.destination, self.sdk)
        self.assertEqual((self.destination / product.METADATA).read_bytes(), receipt)

    def test_missing_source_never_publishes_partial_stage(self):
        (self.source / "main.mjs").unlink()
        with self.assertRaises((RuntimeError, OSError)):
            product.prepare(self.destination, self.sdk)
        self.assertFalse(self.destination.exists())
        self.assertEqual(list(self.owned.glob("product-prepare-*")), [])

    def test_wrapper_missing_modified_and_extra_files_are_rejected(self):
        self.prepare()
        main = self.destination / product.PRODUCT_ROOT / "main.mjs"
        original = main.read_bytes()
        for change in ("modified", "missing", "extra"):
            with self.subTest(change=change):
                main.write_bytes(original)
                extra = main.with_name("unreviewed.mjs")
                if change == "modified":
                    main.write_bytes(b"modified")
                elif change == "missing":
                    main.unlink()
                else:
                    extra.write_bytes(b"extra")
                with self.assertRaisesRegex(RuntimeError, "inventory|integrity"):
                    product.verify_stage(self.destination, self.sdk)
                extra.unlink(missing_ok=True)

    def test_receipt_rejects_schema_protocol_entry_and_source_changes(self):
        baseline = self.prepare()
        for changes in ({"schema": "other"}, {"protocol_version": 2},
                        {"entry": "../outside"}, {"sources": []}, {"files": []}):
            with self.subTest(changes=changes):
                runtime.write_json(self.destination / product.METADATA, {**baseline, **changes})
                with self.assertRaises(RuntimeError):
                    product.verify_stage(self.destination, self.sdk)

    def test_sdk_wrong_platform_recipe_and_raw_receipt_replacement_are_rejected(self):
        self.prepare()
        original = (self.sdk / runtime.METADATA).read_bytes()
        for changes in ({"target": "linux-x64"}, {"pi_version": "0.0.0"},
                        {"node_version": "0.0.0"}, {"recipes": {}}, {"node_source": {}}):
            with self.subTest(changes=changes):
                runtime.write_json(self.sdk / runtime.METADATA, {**self.sdk_metadata, **changes})
                with self.assertRaisesRegex(RuntimeError, "SDK|recipe"):
                    product.verify_stage(self.destination, self.sdk)
        (self.sdk / runtime.METADATA).write_bytes(original + b"\n")
        with self.assertRaisesRegex(RuntimeError, "SDK|receipt"):
            product.verify_stage(self.destination, self.sdk)

    def test_receipt_only_cannot_hide_missing_or_modified_sdk_payload(self):
        self.prepare()
        combined = self.owned / "installed"
        shutil.copytree(self.destination, combined)
        shutil.copytree(self.sdk, combined, dirs_exist_ok=True)
        with patch.object(runtime, "verify_stage", side_effect=RuntimeError("SDK payload integrity differs")) as full:
            with self.assertRaisesRegex(RuntimeError, "SDK payload"):
                product.verify_install(combined)
            full.assert_called_once_with(combined)
        with patch.object(runtime, "verify_stage", return_value=self.sdk_metadata) as full:
            self.assertEqual(product.verify_install(combined)["schema"], "sicnu.pi-product/1")
            full.assert_called_once_with(combined)

    def test_nofollow_product_and_receipt_junctions_preserve_external_marker(self):
        for placement in ("prefix", "component", "nested", "receipt-parent", "sdk-receipt-parent"):
            with self.subTest(placement=placement):
                work = self.owned / placement
                prefix = work / "prefix"
                sdk = work / "sdk"
                external = work / "external"
                external.mkdir(parents=True)
                marker = external / "must-remain.bin"
                marker.write_bytes(b"external marker")
                if placement == "prefix":
                    link = prefix
                elif placement == "component":
                    link = prefix / product.PRODUCT_ROOT
                elif placement == "nested":
                    link = prefix / product.PRODUCT_ROOT / "nested"
                elif placement == "receipt-parent":
                    link = prefix / "share"
                else:
                    link = sdk / "share"
                link.parent.mkdir(parents=True, exist_ok=True)
                if placement in ("receipt-parent", "sdk-receipt-parent"):
                    (prefix / product.PRODUCT_ROOT).mkdir(parents=True)
                    for name in product.PRODUCT_FILES:
                        shutil.copy2(self.source / name, prefix / product.PRODUCT_ROOT / name)
                    if placement == "sdk-receipt-parent":
                        # Get through the valid wrapper receipt to the SDK link.
                        self.prepare()
                        shutil.copytree(self.destination / "share", prefix / "share")
                self.assertTrue(link.parent.resolve().is_relative_to(work.resolve()))
                try:
                    self.create_link(link, external)
                    real_scandir = os.scandir

                    def guarded_scandir(path):
                        self.assertFalse(Path(path).resolve().is_relative_to(external))
                        return real_scandir(path)

                    with patch.object(runtime.os, "scandir", side_effect=guarded_scandir):
                        with self.assertRaisesRegex(RuntimeError, "link|reparse"):
                            product.verify_stage(prefix, sdk)
                    self.assertEqual(marker.read_bytes(), b"external marker")
                finally:
                    if link.exists() or link.is_symlink():
                        os.rmdir(link) if os.name == "nt" else link.unlink()
                    self.assertEqual(marker.read_bytes(), b"external marker")

    def test_output_parent_junction_refuses_before_creating_outside_directories(self):
        external = self.owned / "external-output"
        external.mkdir()
        marker = external / "must-remain.bin"
        marker.write_bytes(b"external marker")
        link = self.owned / "output-junction"
        try:
            self.create_link(link, external)
            with self.assertRaisesRegex(RuntimeError, "link|reparse"):
                product.prepare(link / "must-not-create" / "product", self.sdk)
            self.assertEqual(list(external.iterdir()), [marker])
        finally:
            if link.exists() or link.is_symlink():
                os.rmdir(link) if os.name == "nt" else link.unlink()
            self.assertEqual(marker.read_bytes(), b"external marker")

    def create_link(self, link, external):
        if os.name != "nt":
            link.symlink_to(external, target_is_directory=True)
            return
        environment = os.environ.copy()
        environment["SICNU_TEST_JUNCTION_LINK"] = str(link)
        environment["SICNU_TEST_JUNCTION_TARGET"] = str(external)
        powershell = Path(os.environ["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        result = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-Command",
                                 "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:SICNU_TEST_JUNCTION_LINK -Target $env:SICNU_TEST_JUNCTION_TARGET | Out-Null"],
                                env=environment, capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
