#!/usr/bin/env python3
# conformance.py — independent oracle for the bundle manifest contract.
#
# Synthesizes golden bundle trees at runtime (repo convention: fixtures are
# synthesized, not committed), writes manifest.json with digests computed
# HERE — never by the implementation under test — and drives
# scripts/verify_bundle_manifest.py as a subprocess with the harness cwd set
# outside the bundle (the "verify in another directory" requirement).
#
# The PowerShell twin (scripts/bundle_manifest.ps1 Test-Bundle) runs a
# tamper expectation through the same fixture builder when pwsh is available;
# on hosts without pwsh that lane prints a not-executed line and is skipped
# (the canonical lane itself always runs the full scenario matrix).
#
# Exit 0 = every executed lane passed.
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

PASS = "BUNDLE VERIFY PASS"
FAIL = "BUNDLE VERIFY FAIL"


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def write_manifest(root, schema, files, required, ceiling=250, extra=None):
    manifest = {
        "schema": schema,
        "bundle_version": "9.9-test",
        "created_utc": "2026-09-16T00:00:00+00:00",
        "size_ceiling_mb": ceiling,
        "required": required,
        "files": files,
    }
    if extra:
        manifest.update(extra)
    with open(os.path.join(root, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")


def build_valid_tree(root, schema):
    """A small deterministic bundle: two nested payload files + one top-level
    script. files[] digests are computed by THIS oracle."""
    os.makedirs(os.path.join(root, "data", "samples"), exist_ok=True)
    os.makedirs(os.path.join(root, "tools"), exist_ok=True)
    payloads = {
        "data/samples/a.tif": b"RASTER-A" * 64,
        "data/samples/b.tif": bytes(range(256)) * 8,
        "tools/verify_bundle_manifest.py": b"# shipped verifier copy\n",
        "RUN.sh": b"#!/bin/sh\nexit 0\n",
    }
    files = []
    for rel in sorted(payloads):
        target = os.path.join(root, rel)
        with open(target, "wb") as fh:
            fh.write(payloads[rel])
        files.append({"path": rel, "bytes": len(payloads[rel]),
                      "sha256": sha256_bytes(payloads[rel])})
    required = ["data/samples/", "tools/", "RUN.sh", "manifest.json"]
    v2_extra = {}
    if schema == "sicnu.offline_bundle/2":
        v2_extra = {
            "components": {"gdal": {"version": "3.13.3", "source": "host"}},
            "build_options": {"CMAKE_BUILD_TYPE": "Debug"},
            "compat": {"min_reader_schema": 1, "bundle_kind": "lab-cli"},
        }
    write_manifest(root, schema, files, required, extra=v2_extra)
    return files


def run_verifier(script, bundle, expect_cwd):
    """Run the canonical verifier from a cwd OUTSIDE the bundle."""
    proc = subprocess.run(
        [sys.executable, script, bundle],
        cwd=expect_cwd, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def check(name, cond, detail=""):
    if cond:
        print("  ok: %s" % name)
        CHECKS.append(True)
        return True
    print("  FAIL: %s %s" % (name, detail))
    CHECKS.append(False)
    return False


CHECKS = []


def scenario_valid(script, work):
    for schema in ("sicnu.offline_bundle/1", "sicnu.offline_bundle/2"):
        root = os.path.join(work, "valid-%s" % schema.split("/")[1])
        os.makedirs(root)
        build_valid_tree(root, schema)
        rc, out = run_verifier(script, root, work)
        check("valid %s verifies from another cwd" % schema, rc == 0 and PASS in out, out)


def scenario_tamper(script, work):
    root = os.path.join(work, "tampered")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    with open(os.path.join(root, "data/samples/a.tif"), "ab") as fh:
        fh.write(b"tampered")
    rc, out = run_verifier(script, root, work)
    # A size-first walker reports the size mismatch before hashing; the
    # contract is that the offending file is named either way.
    named = ("size mismatch data/samples/a.tif" in out
             or "sha256 mismatch data/samples/a.tif" in out)
    check("tampered content fails", rc == 1 and FAIL in out, out)
    check("tamper names the file", named, out)


def scenario_missing(script, work):
    root = os.path.join(work, "missing")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    os.remove(os.path.join(root, "data/samples/b.tif"))
    rc, out = run_verifier(script, root, work)
    check("missing file fails", rc == 1 and "missing data/samples/b.tif" in out, out)


def scenario_extra(script, work):
    root = os.path.join(work, "extra")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    with open(os.path.join(root, "smuggled.txt"), "wb") as fh:
        fh.write(b"not in manifest")
    rc, out = run_verifier(script, root, work)
    check("unlisted file fails", rc == 1 and "unlisted file: smuggled.txt" in out, out)


def scenario_unsafe(script, work):
    root = os.path.join(work, "unsafe")
    os.makedirs(root)
    files = build_valid_tree(root, "sicnu.offline_bundle/2")
    files.append({"path": "../../etc/passwd", "bytes": 1, "sha256": "0" * 64})
    write_manifest(root, "sicnu.offline_bundle/2", files,
                   ["data/samples/", "tools/", "RUN.sh", "manifest.json"])
    rc, out = run_verifier(script, root, work)
    check("unsafe path fails", rc == 1 and "unsafe manifest path" in out, out)


def scenario_required_empty(script, work):
    root = os.path.join(work, "required-empty")
    os.makedirs(root)
    files = build_valid_tree(root, "sicnu.offline_bundle/2")
    write_manifest(root, "sicnu.offline_bundle/2", files,
                   ["data/samples/", "tools/", "RUN.sh", "manifest.json",
                    "data/absent-dir/"])
    rc, out = run_verifier(script, root, work)
    check("empty required prefix fails",
          rc == 1 and "required prefix empty: data/absent-dir/" in out, out)


def scenario_unsafe_required(script, work):
    # lab platform 12.0: required[] entries get the same containment rule as
    # files[] — an escaping or drive-qualified entry is a finding, never a
    # probe outside the bundle.
    for label, entry in (
        ("parent-escape", "../outside"),
        ("dotdot-segment", "data/../../etc"),
        ("absolute", "/etc/passwd"),
        ("drive-qualified", "C:/Windows/System32"),
    ):
        root = os.path.join(work, "unsafe-required-%s" % label)
        os.makedirs(root)
        files = build_valid_tree(root, "sicnu.offline_bundle/2")
        write_manifest(root, "sicnu.offline_bundle/2", files,
                       ["data/samples/", "tools/", "RUN.sh", "manifest.json",
                        entry])
        rc, out = run_verifier(script, root, work)
        check("unsafe required %s fails" % label,
              rc == 1 and "unsafe required entry" in out, out)


def scenario_ceiling(script, work):
    root = os.path.join(work, "ceiling")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    # Lower the declared ceiling below the payload size with a full rewrite.
    manifest_path = os.path.join(root, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["size_ceiling_mb"] = 0
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh)
    rc, out = run_verifier(script, root, work)
    check("ceiling breach fails", rc == 1 and "exceeds ceiling 0 MB" in out, out)


def scenario_future_schema(script, work):
    root = os.path.join(work, "future")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    manifest_path = os.path.join(root, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["schema"] = "sicnu.offline_bundle/3"
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh)
    rc, out = run_verifier(script, root, work)
    check("future major schema refuses with exit 2", rc == 2, out)
    check("refusal names the supported majors",
          "sicnu.offline_bundle/1" in out and "sicnu.offline_bundle/2" in out, out)


def scenario_min_reader(script, work):
    root = os.path.join(work, "min-reader")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    manifest_path = os.path.join(root, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["compat"]["min_reader_schema"] = 3
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh)
    rc, out = run_verifier(script, root, work)
    check("min_reader_schema beyond this reader refuses (exit 2)",
          rc == 2 and "min_reader_schema 3" in out, out)


def scenario_missing_manifest(script, work):
    root = os.path.join(work, "no-manifest")
    os.makedirs(root)
    rc, out = run_verifier(script, root, work)
    check("missing manifest refuses with exit 2", rc == 2 and "manifest.json" in out, out)


def scenario_symlink_escape(script, work):
    root = os.path.join(work, "symlink")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    outside = os.path.join(work, "outside.secret")
    with open(outside, "wb") as fh:
        fh.write(b"secret")
    link = os.path.join(root, "tools", "leak")
    try:
        os.symlink(outside, link)
    except OSError:
        print("  not-executed: symlink unavailable on this host")
        return
    # Add the link to files[] with the TARGET's true digest: reading the link
    # reads outside the bundle — this must be flagged, not silently hashed.
    manifest_path = os.path.join(root, "manifest.json")
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["files"].append({"path": "tools/leak", "bytes": 6,
                              "sha256": sha256_file(outside)})
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh)
    rc, out = run_verifier(script, root, work)
    check("escaping symlink flagged", rc == 1 and "symlink escapes bundle" in out, out)


def scenario_ps_lane(script, work):
    """Mirror the identical expectations through scripts/bundle_manifest.ps1
    Test-Bundle. Skipped (not-executed) where pwsh is absent."""
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(script))))
    ps1 = os.path.join(repo, "scripts", "bundle_manifest.ps1")
    pwsh = shutil.which("pwsh") or shutil.which("powershell")
    if not pwsh or not os.path.isfile(ps1):
        print("  not-executed: PS verify lane (pwsh unavailable on this host)")
        return
    root = os.path.join(work, "ps-tampered")
    os.makedirs(root)
    build_valid_tree(root, "sicnu.offline_bundle/2")
    with open(os.path.join(root, "data/samples/a.tif"), "ab") as fh:
        fh.write(b"tampered")
    proc = subprocess.run([pwsh, "-NoProfile", "-File", ps1, "-Verify", root],
                          cwd=work, capture_output=True, text=True)
    check("PS lane: tampered bundle fails", proc.returncode != 0 and FAIL in proc.stdout,
          proc.stdout + proc.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True,
                        help="path to scripts/verify_bundle_manifest.py")
    ns = parser.parse_args()
    ns.script = os.path.abspath(ns.script)  # subprocesses run from elsewhere
    work = tempfile.mkdtemp(prefix="bundle-conformance-")
    try:
        for scenario in (scenario_valid, scenario_tamper, scenario_missing,
                         scenario_extra, scenario_unsafe, scenario_required_empty,
                         scenario_unsafe_required,
                         scenario_ceiling, scenario_future_schema,
                         scenario_min_reader, scenario_missing_manifest,
                         scenario_symlink_escape, scenario_ps_lane):
            print("[%s]" % scenario.__name__)
            scenario(ns.script, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    failed = CHECKS.count(False)
    print("conformance: %d checks, %d failed" % (len(CHECKS), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
