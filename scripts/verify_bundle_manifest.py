#!/usr/bin/env python3
# verify_bundle_manifest.py — canonical verifier for sicnu.offline_bundle manifests.
#
# Contract: packaging/OFFLINE_BUNDLE.md. Single source of truth for the verify
# rules on Linux, inside the bundle (tools/verify_bundle_manifest.py is a copy)
# and in tests (tests/fixtures/bundle_manifest/conformance.py drives this file
# as a subprocess). scripts/bundle_manifest.ps1 mirrors these rules for
# Windows, where python3 is not a runtime assumption.
#
# Schemas: accepts /1 (legacy, files+required+ceiling) and /2 (adds optional
# components/build_options/compat sections and the in-bundle Linux verifier in
# required[]). Any other major is refused with a typed message naming the
# supported majors — a newer bundle must never half-verify.
#
# Exit codes: 0 verified; 1 verified-and-failed (findings printed); 2 cannot
# verify (missing/invalid manifest or unsupported schema).
import argparse
import hashlib
import json
import os
import sys

SUPPORTED_SCHEMAS = ("sicnu.offline_bundle/1", "sicnu.offline_bundle/2")
V2_SCHEMAS = ("sicnu.offline_bundle/2",)
DEFAULT_CEILING_MB = 250


class VerifyResult(object):
    def __init__(self, ok, findings, summary, file_count, total_bytes):
        self.ok = ok
        self.findings = findings      # list[str], without the verdict line
        self.summary = summary        # verdict line, "" when unverifiable
        self.file_count = file_count
        self.total_bytes = total_bytes


def _digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _validate_v2_sections(manifest, findings):
    """Shape checks for the /2-only sections. Values are builder-declared
    provenance, so only structure is enforced — never fabricated content."""
    compat = manifest.get("compat")
    if compat is not None:
        if not isinstance(compat, dict):
            findings.append("compat must be an object")
        else:
            min_reader = compat.get("min_reader_schema")
            if min_reader is not None:
                if not isinstance(min_reader, int) or isinstance(min_reader, bool) or min_reader < 1:
                    findings.append("compat.min_reader_schema must be a positive integer")
            kind = compat.get("bundle_kind")
            if kind is not None and not isinstance(kind, str):
                findings.append("compat.bundle_kind must be a string")
    for key in ("components", "build_options"):
        section = manifest.get(key)
        if section is not None and not isinstance(section, dict):
            findings.append("%s must be an object" % key)


def _unsafe_bundle_rel(rel):
    """True when @p rel (a required[] entry or file path) could escape the
    bundle: empty/non-string, absolute, drive-qualified, or containing a '..'
    segment. Applied to required[] entries too, not just files[] — a hostile
    required path must not turn the existence check into a probe outside the
    bundle."""
    if not isinstance(rel, str) or not rel:
        return True
    if os.path.isabs(rel):
        return True
    normalized = rel.replace("\\", "/")
    if len(normalized) >= 2 and normalized[0].isalpha() and normalized[1] == ":":
        return True  # drive-qualified (C:/...) — absolute on Windows
    parts = normalized.split("/")
    if parts and parts[-1] == "":
        parts = parts[:-1]  # trailing slash of a required prefix
    return ".." in parts


def verify_bundle(root):
    """Verify the bundle at @p root. Never raises on bad input; the result
    carries the verdict. Unverifiable manifests return ok=False with an
    empty summary and exit code 2 decided by main()."""
    manifest_path = os.path.join(root, "manifest.json")
    if not os.path.isfile(manifest_path):
        return VerifyResult(False, ["no manifest.json under %s" % root], "", 0, 0)
    try:
        with open(manifest_path, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
    except (ValueError, UnicodeDecodeError) as exc:
        return VerifyResult(False, ["manifest.json is not valid JSON: %s" % exc], "", 0, 0)
    if not isinstance(manifest, dict):
        return VerifyResult(False, ["manifest.json must be a JSON object"], "", 0, 0)

    schema = manifest.get("schema")
    if schema not in SUPPORTED_SCHEMAS:
        return VerifyResult(
            False,
            ["unsupported manifest schema: %r (this reader supports %s)"
             % (schema, " / ".join(SUPPORTED_SCHEMAS))],
            "", 0, 0)
    is_v2 = schema in V2_SCHEMAS
    if is_v2:
        compat = manifest.get("compat")
        if isinstance(compat, dict):
            min_reader = compat.get("min_reader_schema")
            if isinstance(min_reader, int) and not isinstance(min_reader, bool) \
                    and min_reader > 2:
                # The bundle's semantics need a newer reader than this is:
                # cannot-verify (exit 2), never a half-verification.
                return VerifyResult(
                    False,
                    ["compat.min_reader_schema %s exceeds this reader's newest schema (2)"
                     % min_reader],
                    "", 0, 0)

    bad = []
    listed = set()
    total = 0
    entries = manifest.get("files")
    if not isinstance(entries, list):
        bad.append("files[] must be an array")
        entries = []
    for entry in entries:
        if not isinstance(entry, dict) or "path" not in entry:
            bad.append("malformed files[] entry: %r" % (entry,))
            continue
        rel = entry["path"]
        # Completeness, not authenticity: reject entries that escape the
        # bundle or point at the manifest itself.
        target = os.path.normpath(os.path.join(root, rel))
        if os.path.isabs(rel) or rel.startswith("..") or not target.startswith(root + os.sep):
            bad.append("unsafe manifest path: %s" % rel)
            continue
        if rel == "manifest.json":
            bad.append("manifest.json must not appear in files[]")
            continue
        listed.add(rel)
        if not os.path.isfile(target):
            bad.append("missing %s" % rel)
            continue
        if os.path.islink(target):
            resolved = os.path.realpath(target)
            if not resolved.startswith(root + os.sep):
                bad.append("symlink escapes bundle: %s -> %s" % (rel, resolved))
                continue
        try:
            size = os.path.getsize(target)
        except OSError as exc:
            bad.append("unreadable %s: %s" % (rel, exc))
            continue
        total += size
        if size != entry.get("bytes"):
            bad.append("size mismatch %s: %s != %s" % (rel, size, entry.get("bytes")))
            continue
        try:
            digest = _digest(target)
        except OSError as exc:
            bad.append("unreadable %s: %s" % (rel, exc))
            continue
        if digest != entry.get("sha256"):
            bad.append("sha256 mismatch %s" % rel)

    # files[] must cover EVERY regular file: re-walk and flag unlisted ones.
    # A symlinked directory is pruned and flagged rather than silently skipped
    # (os.walk does not descend into it, so its contents would otherwise be
    # neither hashed nor reported) or followed (hashing outside content).
    for base, dirs, names in os.walk(root):
        for name in dirs:
            joined = os.path.join(base, name)
            if os.path.islink(joined):
                rel = os.path.relpath(joined, root).replace(os.sep, "/")
                resolved = os.path.realpath(joined)
                if resolved.startswith(root + os.sep):
                    bad.append("unlisted symlinked directory: %s" % rel)
                else:
                    bad.append("symlink escapes bundle: %s -> %s" % (rel, resolved))
                dirs.remove(name)
        for name in names:
            rel = os.path.relpath(os.path.join(base, name), root).replace(os.sep, "/")
            if rel != "manifest.json" and rel not in listed:
                bad.append("unlisted file: %s" % rel)

    required_list = manifest.get("required", [])
    if not isinstance(required_list, list):
        bad.append("required[] must be an array")
        required_list = []
    for required in required_list:
        if _unsafe_bundle_rel(required):
            bad.append("unsafe required entry: %r" % (required,))
            continue
        if required.endswith("/"):
            if not any(isinstance(e, dict) and isinstance(e.get("path"), str)
                       and e["path"].replace("\\", "/").startswith(required)
                       for e in entries):
                bad.append("required prefix empty: %s" % required)
        elif not os.path.exists(os.path.join(root, required)):
            bad.append("required missing: %s" % required)

    ceiling = manifest.get("size_ceiling_mb", DEFAULT_CEILING_MB)
    if not isinstance(ceiling, int) or isinstance(ceiling, bool) or ceiling < 0:
        bad.append("size_ceiling_mb must be a non-negative integer")
        ceiling = DEFAULT_CEILING_MB
    mb = total / (1024.0 * 1024.0)
    if is_v2:
        _validate_v2_sections(manifest, bad)

    over = mb > ceiling
    if over:
        bad.append("size %.1f MB exceeds ceiling %d MB" % (mb, ceiling))
    ok = not bad and not over
    summary = "BUNDLE VERIFY %s %s (%d files, %.1f MB / ceiling %d MB)" % (
        "PASS" if ok else "FAIL", root, len(entries), mb, ceiling)
    return VerifyResult(ok, bad, summary, len(entries), total)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify a sicnu.offline_bundle bundle (schemas /1 and /2).")
    parser.add_argument("bundle", help="bundle root directory (contains manifest.json)")
    ns = parser.parse_args(argv)
    root = ns.bundle
    if not os.path.isdir(root):
        print("verify_bundle_manifest: not a directory: %s" % root, file=sys.stderr)
        return 2

    result = verify_bundle(os.path.abspath(root))
    if not result.summary:
        # Unverifiable (missing/invalid manifest or unsupported schema):
        # print the findings and refuse — never a bare PASS/FAIL line.
        for finding in result.findings:
            print(finding)
        return 2
    print(result.summary)
    for finding in result.findings:
        print("  " + finding)
    if result.ok:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
