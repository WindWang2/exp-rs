#!/usr/bin/env python3
"""Archive every receipted application file, including hidden/long-path resources."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import sys
import tempfile
import zipfile


def checked(path):
    status = path.lstat()
    if stat.S_ISLNK(status.st_mode) or getattr(status, "st_file_attributes", 0) & 0x400:
        raise RuntimeError(f"Archive input/output uses a link or reparse point: {path}")
    return status


def inventory(root):
    paths = set()
    pending = [root]
    while pending:
        directory = pending.pop()
        checked(directory)
        with os.scandir(directory) as children:
            for child in children:
                path = Path(child.path)
                status = checked(path)
                if stat.S_ISDIR(status.st_mode):
                    pending.append(path)
                elif stat.S_ISREG(status.st_mode):
                    paths.add(path.relative_to(root).as_posix())
                else:
                    raise RuntimeError(f"Unsupported archive input: {path}")
    return paths


def archive_package(root, archive):
    for path in (root, *root.parents, archive.parent, *archive.parent.parents):
        checked(path)
    if archive.exists() or archive.is_relative_to(root):
        raise RuntimeError("Archive must be fresh and outside the package")
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
    if manifest.get("schema") != "sicnu.windows-package-manifest/1":
        raise RuntimeError("Unsupported application manifest")
    expected = {}
    case_paths = set()
    for entry in manifest["files"]:
        name = entry["path"]
        relative = PurePosixPath(name)
        if (not name or "\\" in name or ":" in name or relative.is_absolute()
                or any(part in ("", ".", "..") for part in name.split("/"))
                or name == "manifest.json" or name.casefold() in case_paths):
            raise RuntimeError("Unsafe or duplicated application manifest path")
        case_paths.add(name.casefold())
        expected[name] = entry
    files = {*expected, "manifest.json"}
    if inventory(root) != files:
        raise RuntimeError("Application inventory differs from manifest")
    pending = None
    created_output = False
    published = False
    try:
        with tempfile.NamedTemporaryFile(prefix=archive.name + "-", suffix=".tmp", dir=archive.parent, delete=False) as temporary:
            pending = Path(temporary.name)
        with zipfile.ZipFile(pending, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as zipped:
            for name in sorted(files):
                source = root / name
                checked(source)
                digest = hashlib.sha256()
                size = 0
                with source.open("rb") as incoming, zipped.open(root.name + "/" + name, "w", force_zip64=True) as outgoing:
                    for block in iter(lambda: incoming.read(1024 * 1024), b""):
                        size += len(block)
                        digest.update(block)
                        outgoing.write(block)
                if name in expected and (size != expected[name]["bytes"] or digest.hexdigest() != expected[name]["sha256"]):
                    raise RuntimeError(f"Application content differs from manifest: {name}")
        if inventory(root) != files:
            raise RuntimeError("Application inventory changed during archiving")
        # Create the destination exclusively: never overwrite another release.
        with pending.open("rb") as source, archive.open("xb") as output:
            created_output = True
            for block in iter(lambda: source.read(1024 * 1024), b""):
                output.write(block)
        published = True
    finally:
        try:
            if created_output and not published:
                archive.unlink(missing_ok=True)
        finally:
            if pending is not None:
                pending.unlink(missing_ok=True)
    print(f"Archived {len(files)} receipted application files: {archive}")


def main():
    # CI consoles may default to CP1252 while package paths contain Unicode.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    args = parser.parse_args()
    try:
        archive_package(args.package_root.absolute(), args.archive.absolute())
    except (OSError, RuntimeError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Application archive refused: {error}\n")


if __name__ == "__main__":
    main()
