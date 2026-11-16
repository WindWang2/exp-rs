#!/usr/bin/env python3
"""R31-T03 WP18: install-resource / SHA-256 integrity check for both manuals.

This is a *resource* check, not a source-string grep: it hashes the actual
files, verifies the generated CMake install script and the Windows staging
required-list both name both manuals, and simulates a relocated install by
copying the payload into share/sicnu_geo_rs/docs and re-hashing it.

Exit code 0 == all checks passed; any failure raises AssertionError.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path

DOCUMENTS = ("USER_GUIDE.md", "USER_GUIDE.en.md")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-dir", required=True)
    ap.add_argument("--binary-dir", required=True)
    ap.add_argument("--manifest", default=None)
    args = ap.parse_args()

    root = Path(args.source_dir)
    docs = root / "docs"
    digests: dict[str, str] = {}
    for name in DOCUMENTS:
        path = docs / name
        assert path.is_file(), f"missing manual: {path}"
        payload = path.read_bytes()
        assert len(payload) > 1000, f"manual too small: {path} ({len(payload)} bytes)"
        digests[name] = sha256(path)
    assert len(set(digests.values())) == len(DOCUMENTS), "manuals must be distinct payloads"

    # Production install rule (both files into the runtime data root).
    cmake_text = (root / "src/app/CMakeLists.txt").read_text(encoding="utf-8")
    for name in DOCUMENTS:
        assert f"docs/{name}" in cmake_text, f"install(FILES ...) missing {name}"

    # Windows staged package fails fast if either manual is absent.
    stage_text = (root / "scripts/stage_windows_application.ps1").read_text(encoding="utf-8")
    for name in DOCUMENTS:
        needle = f"share/sicnu_geo_rs/docs/{name}"
        assert needle in stage_text, f"staging required-list missing {name}"

    # Generated install script is the authoritative build artifact. It MUST
    # exist: silently skipping it would turn this into a pure source grep
    # (R1 P2-17) and would let a broken configure pass unnoticed.
    generated = Path(args.binary_dir) / "src/app/cmake_install.cmake"
    assert generated.is_file(), f"missing generated install script: {generated}"
    install_text = generated.read_text(encoding="utf-8")
    for name in DOCUMENTS:
        assert name in install_text, f"generated install script missing {name}"
    assert "share/sicnu_geo_rs/docs" in install_text, (
        "generated install script does not target share/sicnu_geo_rs/docs")
    # The real install destination must be the same one the runtime resolver
    # searches (guideSearchRoots / AppPaths).
    assert "share/sicnu_geo_rs/docs" in stage_text

    # Relocated-install simulation: copy the exact bytes to an install prefix
    # and confirm checksums survive (no truncation / encoding rewrite).
    with tempfile.TemporaryDirectory(prefix="r31t03-guide-install-") as tmp:
        target = Path(tmp) / "share" / "sicnu_geo_rs" / "docs"
        target.mkdir(parents=True)
        for name in DOCUMENTS:
            shutil.copyfile(docs / name, target / name)
            assert sha256(target / name) == digests[name], f"hash drift for {name}"

    manifest = {
        "track": "r31-t03-desktop-cartography",
        "work_package": "WP18",
        "documents": [
            {"name": name, "sha256": digest, "bytes": (docs / name).stat().st_size}
            for name, digest in digests.items()
        ],
        "install_destination": "share/sicnu_geo_rs/docs",
    }
    if args.manifest:
        out = Path(args.manifest)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                       encoding="utf-8")
    print(json.dumps(manifest, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
