#!/usr/bin/env python3
"""Batch fact extractor for WP-B authoring: per-operator implementation digest.

For each operator id: locate impl .cpp (matrix ground truth), extract
  - schema() parameter declarations (make*Param / properties[...] writes)
  - metadata() body markers
  - determinism/memoryPolicy/streamingHalo overrides
  - RSOperatorError throw sites (code + message)
Writes a compact digest to stdout for authoring evidence.
"""
import json, os, re, sys

ROOT = "/home/kevin/project/exp-rs-processing-meta-r4"
RS = os.path.join(ROOT, "src/operators/rs")

# class -> cpp from matrix
cpp_of = {}
for line in open(os.path.join(ROOT, ".planning/processing-meta-r4/META_COVERAGE_MATRIX.md")):
    if line.startswith("| rs:"):
        c = [x.strip() for x in line.split("|")]
        cpp_of[c[1]] = (c[2], c[4])

def digest(oid):
    hdr, cpp = cpp_of[oid]
    cls = re.sub(r"^rs:", "", oid).title().replace("_", "").replace("-", "")
    path = os.path.join(RS, cpp)
    txt = open(path).read()
    out = [f"\n===== {oid}  ({cpp}, {len(txt.splitlines())} lines) ====="]
    # schema param declarations
    for m in re.finditer(r'(?:Json::Value\s+)?(?:props|properties|root|schema|p)\w*(?:\["(\w+)"\]\s*=\s*)?(?:schema::)?(make\w+Param|make\w+Schema)\s*\(([^;]{0,220})', txt):
        out.append(f"  schema: {m.group(2)}({m.group(3)[:200]})" if m.group(1) is None else f"  schema[{m.group(1)}]: {m.group(2)}({m.group(3)[:200]})")
    # throws
    for m in re.finditer(r'RSOperatorError\s*\(\s*(?:sicnu::operators::)?(?:RSOperatorError::)?(\w+)\s*,\s*"([^"]{0,160})', txt):
        out.append(f"  throw {m.group(1)}: {m.group(2)}")
    # overrides
    for key in (r'determinismGrade\(\)\s*const\s*override\s*{[^}]*}', r'determinism\(\)\s*const\s*override\s*{[^}]*}',
                r'memoryPolicy\(\)\s*const\s*override\s*{[^}]*}', r'streamingHaloPixels\(\)\s*const\s*override\s*{[^}]*}'):
        for m in re.finditer(key, txt):
            out.append("  override: " + " ".join(m.group(0).split())[:150])
    # metadata override presence
    if re.search(r'metadata\(\)\s*const\s*override', txt):
        out.append("  metadata(): overridden")
    if re.search(r'displayName\(\)\s*const\s*override', txt):
        for m in re.finditer(r'return\s*(?:"([^"]{5,120})"\s*;|tr?\("([^"]{5,120})"\))', txt.split("displayName")[1][:400] if "displayName" in txt else ""):
            out.append(f"  displayName: {m.group(1) or m.group(2)}"); break
    # group()
    gm = re.search(r'group\(\)\s*const\s*override\s*{\s*return\s*(?:"([^"]+)"|tr\("[^"]+"\))', txt)
    if gm: out.append(f"  group: {gm.group(1)}")
    dm = re.search(r'description\(\)\s*const\s*override\s*{\s*return\s*(?:"([^"]{5,200})"|[^;]+)', txt)
    if dm and dm.group(1): out.append(f"  description: {dm.group(1)[:200]}")
    return "\n".join(out)

if __name__ == "__main__":
    batches = json.load(open(os.path.join(ROOT, ".planning/processing-meta-r4/batches.json")))
    idx = int(sys.argv[1])
    for oid in batches[idx]:
        print(digest(oid))
