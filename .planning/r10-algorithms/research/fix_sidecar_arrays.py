#!/usr/bin/env python3
"""A1-D1 capability sidecar array-semantics repair (R10-A / S2b).

The capability sidecars under data/processing/algorithm_meta/capability/*.json
flatten array parameters into their element scalar (the generator's parameter
branch dropped isArray/itemType — root cause in
src/agent/harness/capability_catalog.cpp ioRow(), fixed out-of-domain). This
script restores the array semantics INTO THE DATA by reading each operator's
code schema() truth:

  * every `X["type"] = "array"` declaration site in src/operators/rs/*.cpp is
    resolved to (operator id, parameter name, item kind) — element kind from
    `items.type` ("number" -> "numeric", "object" -> "json", absent -> "string");
  * referenceInputSchemaProps(inlineKey, refKey, ...) call sites contribute
    {inlineKey: array/string, libraryMaterials: array/string};
  * the shared addCommonProps/detectorSchema helpers are attributed to the
    operators whose schema() calls them;
  * each sidecar parameter row whose name matches a code-array parameter and
    whose existing `type` equals the element kind gains:
        "array" : true,
        "item_type" : "<element kind>",
    inserted with the sidecar's established style (jsoncpp styled writer,
    2-space indent, `" : "` colons, alphabetical key order, same line endings).

A site whose row cannot be cross-checked (no parameter row with that name and
element kind) is reported for manual review instead of being patched blindly.
Outputs rows (already correct) are skipped. Run `git diff` afterwards and
spot-check at least five files by hand.

Usage:  python fix_sidecar_arrays.py [--dry-run]
"""

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
RS_DIR = REPO / "src" / "operators" / "rs"
SIDECAR_DIR = REPO / "data" / "processing" / "algorithm_meta" / "capability"
APPENDIX = Path(__file__).resolve().parent / "A1_PARITY_DATA.md"
INIT_CPP = RS_DIR / "rs_operators_init.cpp"

KIND_MAP = {"integer": "integer", "string": "string", "number": "numeric",
            "boolean": "boolean", "object": "json", "array": None}

FUNC_HEADER = re.compile(
    r"^[A-Za-z_][\w\s:<>,&*]*?\s+(\w+)::(\w+)\s*\("      # Class::method
    r"|^[A-Za-z_][\w\s:<>,&*]*?\s+(\w+)\s*\(")             # free function
SITE_RE = re.compile(r'^\s*(\w+)\["type"\]\s*=\s*"array"')
DIRECT_CHAIN_RE = re.compile(r'^\s*(props|outputs)\["([^"]+)"\]\["type"\]\s*=\s*"array"')
ASSIGN_RE = re.compile(r'\b(props|outputs)\[([A-Za-z_]\w*|"[^"]+")\]\s*=\s*(\w+)\s*;')
NAME_FIELD_RE = re.compile(r'\b(\w+)\["name"\]\s*=\s*"([^"]+)"')
LAMBDA_ASSIGN_RE = re.compile(r'\b(props|outputs)\["([^"]+)"\]\s*=\s*\[\]\s*\{')
LOOP_HEADER_RE = re.compile(r"for\s*\([^)]*?\b(\w+)\s*:[^)]*\{([^}]*)\}")
ITEMS_CHAIN_RE = re.compile(r'\["items"\]\["type"\]\s*=\s*"(\w+)"')
LOCAL_ITEMS_RE = re.compile(r'\b(\w+)\["type"\]\s*=\s*"(\w+)"')
ITEMS_FROM_LOCAL_RE = re.compile(r'\b(\w+)\["items"\]\s*=\s*(\w+)')
ITEMS_FROM_HELPER_RE = re.compile(r'\b(\w+)\["items"\]\s*=\s*make(\w+)Param')
HELPER_KIND = {"Integer": "integer", "Number": "numeric", "String": "string",
               "Boolean": "boolean"}
REF_PROPS_CALL_RE = re.compile(
    r'referenceInputSchemaProps\(\s*"([^"]+)"\s*,\s*"([^"]+)"', re.S)


def parse_appendix():
    """operator id -> (schema source path, shared_flag) from A1 appendix."""
    mapping = {}
    for line in APPENDIX.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\|\s*\d+\s*\|\s*rs\s*\|\s*`([^`]+)`\s*\|", line)
        if not m:
            continue
        op_id = m.group(1)
        cells = [c.strip() for c in line.split("|")]
        # cells: ['', '#', family, id, class, regpoint, schema, ...]
        if len(cells) < 7:
            continue
        schema_cell = cells[6]
        pm = re.search(r"(src/operators/\S+?\.cpp)", schema_cell)
        if not pm:
            continue
        mapping[op_id] = (pm.group(1), "shared" in schema_cell)
    return mapping


def parse_class_to_id():
    text = INIT_CPP.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(
            r'add\(\s*"([^"]+)"\s*,\s*\[\]\s*\{\s*return\s+std::make_unique<(\w+)>\(\);\s*\}\s*\)',
            text):
        out.setdefault(m.group(2), []).append(m.group(1))
    return out


def function_ranges(lines):
    """Column-0 function headers -> (start, end) line index ranges (exclusive end)."""
    ranges = []
    current = None
    for i, line in enumerate(lines):
        if current is None:
            m = FUNC_HEADER.match(line)
            if m:
                current = {"cls": m.group(1), "name": m.group(2) or m.group(3),
                           "start": i}
        elif line.startswith("}"):
            current["end"] = i + 1
            ranges.append(current)
            current = None
    return ranges


def enclosing_function(ranges, idx):
    for fr in ranges:
        if fr["start"] <= idx < fr["end"]:
            return fr
    return None


def ops_for_function(fr, class_to_id, file_ops, helpers_callers):
    """operator ids a schema function belongs to (direct class or helper callers)."""
    if fr["cls"]:
        ids = class_to_id.get(fr["cls"])
        if ids:
            return ids
        # shared base-class schema (fusion aliases): every op whose appendix
        # schema file is this file inherits it
        return list(file_ops)
    # free helper: attributed to schema() bodies that call it
    callers = helpers_callers.get(fr["name"], set())
    ids = []
    for cls in callers:
        ids.extend(class_to_id.get(cls, []))
    return ids


def resolve_site(lines, idx, var):
    """Return (target, names, item_kind, region) for an array site, or None."""
    # Chained direct assignment: outputs["x"]["type"] = "array"
    dm = DIRECT_CHAIN_RE.match(lines[idx])
    if dm:
        return dm.group(1), [dm.group(2)], None, (idx, idx + 1)

    # Find the props/outputs assignment of `var` near the site (forward for
    # local-var style, backward for lambda style).
    lo, hi = max(0, idx - 20), min(len(lines), idx + 25)
    target = names = None
    stmt_start = idx
    for j in range(lo, hi):
        am = ASSIGN_RE.search(lines[j])
        if am and am.group(3) == var:
            target = am.group(1)
            key = am.group(2)
            if key.startswith('"'):
                names = [key.strip('"')]
            else:
                # loop-generated key: collect the literals from the for header
                for k in range(j - 1, lo - 1, -1):
                    lm = LOOP_HEADER_RE.search(lines[k])
                    if lm and lm.group(1) == key:
                        names = re.findall(r'"([^"]+)"', lm.group(2))
                        break
            stmt_end = j
            break
    else:
        # lambda assigned directly into props/outputs: header precedes the
        # site; the items type is declared INSIDE the lambda body (i.e. after
        # the site), so the region must extend past it to the `}();` closer.
        for j in range(idx - 1, lo - 1, -1):
            lm = LAMBDA_ASSIGN_RE.search(lines[j])
            if lm:
                target, names = lm.group(1), [lm.group(2)]
                stmt_start = j
                stmt_end = idx
                for k in range(idx, min(len(lines), idx + 10)):
                    stmt_end = k
                    if lines[k].strip().startswith("}()"):
                        break
                break
        else:
            return None

    # var may also carry an explicit ["name"] field near the site
    nm = NAME_FIELD_RE.search(" ".join(lines[lo:hi]))
    if nm and nm.group(1) == var:
        names = [nm.group(2)]

    # Item kind: chained ["items"]["type"], a local items var wired into
    # var["items"], or items built by a make*Param helper.
    region = " ".join(lines[stmt_start:stmt_end + 1])
    kind = None
    cm = ITEMS_CHAIN_RE.search(region)
    if cm:
        kind = cm.group(1)
    else:
        wired = None
        for im in ITEMS_FROM_HELPER_RE.finditer(region):
            if im.group(1) == var:
                kind = HELPER_KIND.get(im.group(2))
                break
        if not kind:
            for im in ITEMS_FROM_LOCAL_RE.finditer(region):
                if im.group(1) == var:
                    wired = im.group(2)
                    break
            if wired:
                for lm in LOCAL_ITEMS_RE.finditer(region):
                    if lm.group(1) == wired:
                        kind = lm.group(2)
                        break
    return target, names, kind, (stmt_start, stmt_end)


def extract_array_params(verbose=False):
    """{op_id: {param_name: item_kind}} from code schema() truth."""
    appendix = parse_appendix()
    class_to_id = parse_class_to_id()
    file_to_ops = {}
    for op, (path, _shared) in appendix.items():
        file_to_ops.setdefault(path, []).append(op)

    result = {}
    unresolved = []

    for path, file_ops in sorted(file_to_ops.items()):
        cpp = REPO / path
        if not cpp.exists() or "/rs/" not in path.replace("\\", "/"):
            continue
        text = cpp.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        ranges = function_ranges(lines)

        # helper -> classes whose ::schema() body calls it
        helpers_callers = {}
        for fr in ranges:
            if fr["cls"]:
                body = "\n".join(lines[fr["start"]:fr["end"]])
                for hname in set(re.findall(r"\b(\w+)\s*\(\s*\"|\b(\w+)\s*\(\s*props", body)):
                    name = hname[0] or hname[1]
                    if name != fr["name"]:
                        helpers_callers.setdefault(name, set()).add(fr["cls"])

        # referenceInputSchemaProps call sites: inline key + libraryMaterials
        ref_arrays = {}  # param -> kind, applies file-wide (row presence filters)
        for cm in REF_PROPS_CALL_RE.finditer(text):
            ref_arrays[cm.group(1)] = "string"
        if ref_arrays:
            ref_arrays["libraryMaterials"] = "string"

        for idx, line in enumerate(lines):
            # 1) explicit array sites
            sm = SITE_RE.match(line)
            if sm:
                fr = enclosing_function(ranges, idx)
                var = sm.group(1)
                res = resolve_site(lines, idx, var)
                if res is None or fr is None:
                    unresolved.append(f"{path}:{idx + 1}: var '{var}' unresolved")
                    continue
                target, names, kind, _ = res
                if target != "props" or not names:
                    continue  # outputs are already array-correct
                item_kind = KIND_MAP.get(kind, "string") if kind else "string"
                ops = ops_for_function(fr, class_to_id, file_ops, helpers_callers)
                for op in ops:
                    for name in names:
                        result.setdefault(op, {})[name] = item_kind
                if verbose:
                    print(f"  {path.name}:{idx + 1} {fr['cls'] or fr['name']}: "
                          f"{names} -> {item_kind} (ops={len(ops)})")

        # 2) referenceInputSchemaProps inline arrays (file-wide candidates)
        if ref_arrays:
            for op in file_ops:
                for name, kind in ref_arrays.items():
                    result.setdefault(op, {}).setdefault(name, kind)

    return result, unresolved


def parameters_section_span(lines):
    """Line-index span (start, end) of the io "parameters" array, or None."""
    for i, ln in enumerate(lines):
        if ln.strip() == '"parameters" :':
            # jsoncpp style: the opening '[' follows on the next line
            j = i + 1
            while j < len(lines) and not lines[j].strip():
                j += 1
            if j >= len(lines) or lines[j].strip() != "[":
                continue
            depth = 0
            for k in range(j, len(lines)):
                depth += lines[k].count("[") + lines[k].count("{") \
                    - lines[k].count("]") - lines[k].count("}")
                if k > j and depth <= 0:
                    return (j, k)
            return None
    return None


def patch_sidecars(array_params, dry_run):
    changed_files = []
    changed_rows = 0
    mismatches = []
    missing = []

    for jf in sorted(SIDECAR_DIR.glob("rs-*.json")):
        doc = json.loads(jf.read_text(encoding="utf-8"))
        op_id = doc["id"]  # authoritative
        wanted = array_params.get(op_id, {})

        rows = doc.get("capability", {}).get("io", {}).get("parameters", [])
        row_by_name = {r["name"]: r for r in rows if isinstance(r, dict)}
        to_patch = {}
        for name, kind in wanted.items():
            row = row_by_name.get(name)
            if row is None:
                continue  # not materialized for this operator (or an output)
            if row.get("array"):
                continue  # already fixed
            if row.get("type") != kind:
                mismatches.append(f"{op_id}.{name}: sidecar type "
                                  f"'{row.get('type')}' != code element kind '{kind}'")
                continue
            to_patch[name] = kind
        # rows NOT flagged by code extraction stay scalar (legacy shape)

        if not to_patch:
            continue

        raw = jf.read_bytes()
        nl = "\r\n" if b"\r\n" in raw[:4000] else "\n"
        lines = raw.decode("utf-8").split(nl)
        span = parameters_section_span(lines)
        if span is None:
            missing.append(f"{op_id}: parameters section not found")
            continue

        for name, kind in to_patch.items():
            name_line_idx = None
            for i in range(span[0], span[1]):
                if lines[i].strip() == f'"name" : "{name}",':
                    name_line_idx = i
                    break
            if name_line_idx is None:
                missing.append(f"{op_id}.{name}: row not found in text")
                continue

            indent = len(lines[name_line_idx]) - len(lines[name_line_idx].lstrip())
            row_indent = " " * indent
            # opening brace of the row object (jsoncpp: a lone '{' line above)
            open_idx = name_line_idx
            while not lines[open_idx].strip().startswith("{"):
                open_idx -= 1
            # alphabetical order inside the row: array < default/enum <
            # item_type < name — insert item_type right before "name", then
            # array right after the opening brace.
            lines.insert(name_line_idx, f'{row_indent}"item_type" : "{kind}",')
            lines.insert(open_idx + 1, f'{row_indent}"array" : true,')
            changed_rows += 1

        if not dry_run:
            jf.write_bytes(nl.join(lines).encode("utf-8"))
        changed_files.append(jf.name)

    return changed_files, changed_rows, mismatches, missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    array_params, unresolved = extract_array_params(verbose=args.verbose)
    total_rows = sum(len(v) for v in array_params.values())
    print(f"code truth: {len(array_params)} operators, {total_rows} array param candidates")

    changed_files, changed_rows, mismatches, missing = patch_sidecars(
        array_params, args.dry_run)
    print(f"{'[dry-run] ' if args.dry_run else ''}patched {changed_rows} rows in "
          f"{len(changed_files)} sidecar files")

    if unresolved:
        print("\nUNRESOLVED SITES (manual review):")
        for u in unresolved:
            print("  " + u)
    if mismatches:
        print("\nTYPE MISMATCHES (extraction suspect — NOT patched):")
        for m in mismatches:
            print("  " + m)
    if missing:
        print("\nROWS NOT FOUND IN TEXT (NOT patched):")
        for m in missing:
            print("  " + m)
    if unresolved or mismatches or missing:
        sys.exit(1)


if __name__ == "__main__":
    main()
