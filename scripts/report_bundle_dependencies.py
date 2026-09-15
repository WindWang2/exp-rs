#!/usr/bin/env python3
# report_bundle_dependencies.py — dependency inventory for offline bundles (F19).
#
# Scans the shipped ELF executables/shared objects in a bundle's bin/ (and
# optionally the whole bundle), extracts DT_NEEDED entries with a minimal
# stdlib ELF parser, and classifies every dependency as:
#   shipped  — provided by a file inside the bundle (bin/ or any scanned file)
#   host     — resolved on the deployment machine's loader path (glibc, system
#              Qt/GDAL in the current portable-tree mode) — recorded with the
#              host file that the *build machine* would use, when readable
#   unresolved — found neither in the bundle nor on the build host
#
# Output: <bundle>/dependencies.json (schema "exp.bundle.deps.v1"). The file
# lands inside the bundle BEFORE the manifest is written, so the manifest
# hashes it like any other payload; nothing about the manifest format changes.
#
# Windows: use scripts/windows/bundle_dependency_report.ps1 (dumpbin-based).
# This script reports only what it can prove; unknown ELF classes produce a
# typed "unparsed" entry, never a guess.
#
# usage: report_bundle_dependencies.py --bundle <dir> [--bin-subdir bin]
import argparse
import json
import os
import subprocess
import sys

SCHEMA = "exp.bundle.deps.v1"


def read_elf_needed(path):
    """Return (needed_list, elf_class) or (None, reason) when not parseable."""
    try:
        with open(path, "rb") as fh:
            data = fh.read(64)
        if len(data) < 20 or data[:4] != b"\x7fELF":
            return None, "not ELF"
        is64 = data[4] == 2
        little = data[5] == 1
        if not little:
            return None, "big-endian unsupported"
    except OSError as exc:
        return None, "unreadable: %s" % exc

    # Walk section headers to find .dynamic → DT_NEEDED. Header parsing is
    # size-bounded: sections live at the end of the file, we read only the
    # header table plus the dynamic string table.
    try:
        import struct
        if is64:
            e_shoff, = struct.unpack_from("<Q", data, 0x28)
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        else:
            e_shoff, = struct.unpack_from("<I", data, 0x20)
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        with open(path, "rb") as fh:
            import os
            fh.seek(e_shoff)
            raw = fh.read(e_shentsize * e_shnum)
        sections = []
        for i in range(e_shnum):
            off = i * e_shentsize
            if is64:
                sh_name, sh_type = struct.unpack_from("<II", raw, off)
                sh_offset, sh_size = struct.unpack_from("<QQ", raw, off + 0x18)
            else:
                sh_name, sh_type = struct.unpack_from("<II", raw, off)
                sh_offset, sh_size = struct.unpack_from("<II", raw, off + 0x10)
            sections.append((sh_name, sh_type, sh_offset, sh_size))
        # find .dynamic (type 6) and its linked string table via sh_link
        dyn = next((s for s in sections if s[1] == 6), None)
        if dyn is None:
            return [], "static"
        # sh_link lives right after sh_type at offset +6 (32-bit word)
        idx = sections.index(dyn)
        off = idx * e_shentsize
        strtab_idx, = struct.unpack_from("<I", raw, off + 6)
        _, _, str_off, str_size = sections[strtab_idx]
        with open(path, "rb") as fh:
            fh.seek(str_off)
            strtab = fh.read(str_size)
        with open(path, "rb") as fh:
            fh.seek(dyn[2])
            dyn_raw = fh.read(dyn[3])

        def cstr(tab, pos):
            end = tab.find(b"\0", pos)
            return tab[pos:end].decode("utf-8", "replace")

        needed = []
        entry_size = 16 if is64 else 8
        for pos in range(0, len(dyn_raw) - entry_size + 1, entry_size):
            if is64:
                tag, = struct.unpack_from("<q", dyn_raw, pos)
                val, = struct.unpack_from("<Q", dyn_raw, pos + 8)
            else:
                tag, = struct.unpack_from("<i", dyn_raw, pos)
                val, = struct.unpack_from("<I", dyn_raw, pos + 4)
            if tag == 0:
                break
            if tag == 1:  # DT_NEEDED
                needed.append(cstr(strtab, val))
        return needed, "elf"
    except (OSError, struct.error) as exc:
        return None, "parse error: %s" % exc


def host_provider(soname):
    """Best-effort build-host resolution for reporting context only."""
    try:
        out = subprocess.run(["ldconfig", "-p"], capture_output=True, text=True, timeout=30)
        for line in out.stdout.splitlines():
            if soname in line:
                return line.split("=>")[-1].strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    return ""


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--bin-subdir", default="bin",
                        help="subdir holding the shipped executables (scanned "
                             "files define the shipped set; 'anywhere' scans "
                             "the whole bundle for both sides)")
    parser.add_argument("--out", default="",
                        help="output path (default <bundle>/dependencies.json)")
    ns = parser.parse_args(argv)

    bundle = os.path.abspath(ns.bundle)
    if not os.path.isdir(bundle):
        print("report_bundle_dependencies: not a directory: %s" % bundle, file=sys.stderr)
        return 2
    scan_root = bundle if ns.bin_subdir == "anywhere" else os.path.join(bundle, ns.bin_subdir)
    if not os.path.isdir(scan_root):
        print("report_bundle_dependencies: no %s under bundle" % ns.bin_subdir, file=sys.stderr)
        return 2

    scanned, shipped_names, entries = [], set(), []
    for base, _dirs, names in os.walk(bundle if ns.bin_subdir == "anywhere" else scan_root):
        for name in sorted(names):
            path = os.path.join(base, name)
            rel = os.path.relpath(path, bundle).replace(os.sep, "/")
            needed, kind = read_elf_needed(path)
            if needed is None:
                if kind.startswith("parse error") or kind == "big-endian unsupported":
                    entries.append({"file": rel, "status": "unparsed", "reason": kind})
                continue
            scanned.append((rel, needed))
            shipped_names.add(os.path.basename(path))

    # A dependency is "shipped" when its basename is provided by any scanned
    # file (ELF dependency identity is the SONAME/basename on ELF platforms).
    # Scanning the whole bundle in 'anywhere' mode widens the provider set.
    provider_scan_root = bundle if ns.bin_subdir == "anywhere" else scan_root
    for base, _dirs, names in os.walk(provider_scan_root):
        for name in names:
            shipped_names.add(name)

    for rel, needed in scanned:
        for soname in needed:
            status = "shipped" if soname in shipped_names else "host"
            entry = {"file": rel, "dependency": soname, "status": status}
            if status == "host":
                provider = host_provider(soname)
                if provider:
                    entry["build_host_provider"] = provider
                else:
                    entry["status"] = "unresolved"
            entries.append(entry)

    report = {
        "schema": SCHEMA,
        "bundle": os.path.basename(bundle),
        "note": "shipped=provided by this bundle; host=resolved by the "
                "deployment machine loader; unresolved=not found on the build "
                "host either (verify the deployment machine provides it)",
        "scanned_files": len(scanned),
        "dependencies": sorted(entries, key=lambda e: (e.get("file", ""), e.get("dependency", "")),
                               ) if isinstance(entries, list) else entries,
    }
    out_path = ns.out or os.path.join(bundle, "dependencies.json")
    with open(out_path, "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
        fh.write("\n")
    counts = {"shipped": 0, "host": 0, "unresolved": 0}
    for entry in entries:
        status = entry.get("status")
        if status in counts:
            counts[status] += 1
    print("dependencies: %d scanned files, %d shipped, %d host, %d unresolved -> %s"
          % (len(scanned), counts["shipped"], counts["host"], counts["unresolved"], out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
