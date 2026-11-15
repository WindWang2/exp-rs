#!/usr/bin/env python3
"""Stage the small product-owned Pi wrapper without rebuilding the pinned SDK.

Preparation and --verify-stage read only the SDK receipt, never its large
payload. --verify-install validates the complete installed SDK and wrapper.
No action downloads, invokes npm, or executes JavaScript.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pi_packaging_runtime", ROOT / "scripts/prepare_pi_runtime.py")
runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runtime)
SOURCE = ROOT / "pi/product-runtime"
PRODUCT_FILES = ("checkpoint.mjs", "controlled-session.mjs", "main.mjs", "protocol.mjs")
PRODUCT_ROOT = Path("libexec/sicnu-geo-rs/pi-product")
METADATA = Path("share/sicnu-geo-rs/pi-product.json")
PROTOCOL_VERSION = 1


def recipe_digest(recipes):
    """SHA256 of UTF-8 sorted, compact JSON; no BOM or trailing newline."""
    canonical = json.dumps(recipes, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def expected_recipe_digest():
    pi, _, _, _ = runtime.load_locks()
    return recipe_digest(runtime.recipe_hashes(pi))


def checked_file(prefix, relative):
    """Refuse directory junctions before reading a receipt or source file."""
    directory = prefix
    if not stat.S_ISDIR(runtime.checked_status(directory).st_mode):
        raise RuntimeError(f"Pi product prefix is not a directory: {prefix}")
    for part in relative.parts[:-1]:
        directory /= part
        if not stat.S_ISDIR(runtime.checked_status(directory).st_mode):
            raise RuntimeError(f"Pi product parent is not a directory: {directory}")
    path = directory / relative.name
    if not stat.S_ISREG(runtime.checked_status(path).st_mode):
        raise RuntimeError(f"Pi product requires a regular file: {path}")
    return path


def sdk_identity(prefix):
    """Check pinned identity from the small receipt, without claiming payload integrity."""
    receipt = checked_file(prefix, runtime.METADATA)
    metadata = runtime.read_json(receipt)
    pi, node, _, _ = runtime.load_locks()
    target = runtime.native_target()
    recipes = runtime.recipe_hashes(pi)
    if (metadata.get("schema") != "sicnu.pi-runtime/1" or metadata.get("target") != target
            or metadata.get("pi_version") != pi["pi_version"]
            or metadata.get("node_version") != node["node_version"]
            or metadata.get("node_source") != node["distributions"][target]
            or metadata.get("recipes") != recipes):
        raise RuntimeError("Pi product SDK receipt does not match the pinned native recipe")
    return {"target": target, "pi_version": pi["pi_version"], "node_version": node["node_version"],
            "recipe_digest": recipe_digest(recipes), "receipt_sha256": runtime.sha256(receipt)}


def source_records():
    return [{"path": "pi/product-runtime/" + name,
             "sha256": runtime.sha256(checked_file(SOURCE, Path(name)))} for name in PRODUCT_FILES]


def expected_files(sources):
    return [{"path": (PRODUCT_ROOT / name).as_posix(), "sha256": source["sha256"],
             "size": checked_file(SOURCE, Path(name)).stat().st_size}
            for name, source in zip(PRODUCT_FILES, sources)]


def verify_stage(prefix, sdk_stage):
    # Inventory first: no receipt or package file is read through a payload link.
    actual = runtime.file_inventory(prefix, roots=(PRODUCT_ROOT,))
    metadata = runtime.read_json(checked_file(prefix, METADATA))
    if (set(metadata) != {"schema", "protocol_version", "entry", "requires_sdk", "sources", "files"}
            or metadata.get("schema") != "sicnu.pi-product/1"
            or type(metadata.get("protocol_version")) is not int
            or metadata["protocol_version"] != PROTOCOL_VERSION
            or metadata.get("entry") != (PRODUCT_ROOT / "main.mjs").as_posix()):
        raise RuntimeError("Pi product receipt schema, protocol or entry is unsupported")
    if metadata.get("requires_sdk") != sdk_identity(sdk_stage):
        raise RuntimeError("Pi product SDK receipt binding differs")
    sources = source_records()
    if metadata.get("sources") != sources:
        raise RuntimeError("Pi product source recipe differs from its receipt")
    if metadata.get("files") != expected_files(sources):
        raise RuntimeError("Pi product receipt inventory differs from its allowlisted source recipe")
    runtime.verify_inventory(prefix, metadata["files"], actual)
    return metadata


def verify_install(prefix):
    # The fast identity check alone cannot establish SDK payload integrity.
    runtime.verify_stage(prefix)
    return verify_stage(prefix, prefix)


def prepare(destination, sdk_stage):
    identity = sdk_identity(sdk_stage)
    sources = source_records()
    if os.path.lexists(destination):
        verify_stage(destination, sdk_stage)
        print(f"Verified existing Pi product stage: {destination}")
        return
    # Check existing ancestors before mkdir can write through an output link.
    for parent in (destination.parent, *destination.parent.parents):
        try:
            status = runtime.checked_status(parent)
        except FileNotFoundError:
            continue
        if not stat.S_ISDIR(status.st_mode):
            raise RuntimeError(f"Pi product output parent is not a directory: {parent}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=destination.name + "-prepare-", dir=destination.parent) as temporary:
        prefix = Path(temporary) / "prefix"
        payload = prefix / PRODUCT_ROOT
        payload.mkdir(parents=True)
        for name in PRODUCT_FILES:
            shutil.copy2(checked_file(SOURCE, Path(name)), payload / name)
        runtime.write_json(prefix / METADATA, {
            "schema": "sicnu.pi-product/1", "protocol_version": PROTOCOL_VERSION,
            "entry": (PRODUCT_ROOT / "main.mjs").as_posix(), "requires_sdk": identity,
            "sources": sources, "files": expected_files(sources)})
        verify_stage(prefix, sdk_stage)
        prefix.rename(destination)
    print(f"Prepared Pi product wrapper: {destination}")


def main():
    # CI consoles may default to CP1252 while package paths contain Unicode.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--destination", type=Path)
    action.add_argument("--verify-stage", type=Path)
    action.add_argument("--verify-install", type=Path)
    action.add_argument("--print-recipe-digest", action="store_true",
                        help="platform-independent pinned recipe digest; no SDK access")
    parser.add_argument("--sdk-stage", type=Path, help="pinned SDK stage whose raw receipt is bound")
    args = parser.parse_args()
    if (args.destination or args.verify_stage) and not args.sdk_stage:
        parser.error("--sdk-stage is required for wrapper preparation or stage verification")
    if (args.verify_install or args.print_recipe_digest) and args.sdk_stage:
        parser.error("--sdk-stage is only used with --destination or --verify-stage")
    try:
        if args.print_recipe_digest:
            print(expected_recipe_digest())
        elif args.destination:
            prepare(args.destination.absolute(), args.sdk_stage.absolute())
        elif args.verify_stage:
            verify_stage(args.verify_stage.absolute(), args.sdk_stage.absolute())
            print("Pi product wrapper integrity and SDK receipt binding passed (SDK payload not scanned)")
        else:
            verify_install(args.verify_install.absolute())
            print("Complete installed Pi SDK and product wrapper integrity passed")
    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Pi product preparation failed: {error}\n")


if __name__ == "__main__":
    main()
