#!/usr/bin/env python3
"""Apply authored enrichment JSON files to capability sidecars.

Merges authoring/c*.json into data/processing/algorithm_meta/capability/rs-*.json:
  - prerequisites/limitations: APPEND entries not already present (derived
    descriptor entries stay first; gen-meta merges authored after derived).
  - applicability/teaching_use: set only when currently empty.
Never touches derived keys. Run BEFORE `capability_knowledge_tool gen-meta`
so the tool's canonical re-render normalizes formatting.
"""
import glob, json, os, sys

ROOT = "/home/kevin/project/exp-rs-processing-meta-r4"
CAP = os.path.join(ROOT, "data/processing/algorithm_meta/capability")
AUTH = ("prerequisites", "limitations")

def fname(oid):
    return oid.replace(":", "-").replace("_", "-") + ".json"

applied = 0
patterns = sys.argv[1:] or ["c*.json"]
files = sorted(set(f for p in patterns for f in glob.glob(os.path.join(ROOT, ".planning/processing-meta-r4/authoring", p))))
for path in files:
    for oid, entries in json.load(open(path)).items():
        fp = os.path.join(CAP, fname(oid))
        doc = json.load(open(fp))
        cap = doc["capability"]
        for key, value in entries.items():
            if key in AUTH:
                cur = cap.setdefault(key, [])
                for item in value:
                    if item not in cur:
                        cur.append(item)
            else:
                if cap.get(key):
                    sys.exit(f"refusing to overwrite non-empty authored key {oid}.{key}")
                cap[key] = value
        json.dump(doc, open(fp, "w"), ensure_ascii=False, indent=2)
        open(fp, "a").write("\n")
        applied += 1
print("applied to", applied, "sidecars (raw JSON; run capability_knowledge_tool gen-meta to normalize)")
