#!/usr/bin/env python3
# scripts/check_lab_registry.py — the lab registry gate (classroom-safety 13.0).
#
# data/labs/lab-registry.json is the single authority for lab identity. This
# script is what makes that claim enforceable: it re-derives the projections
# (document ids, pack basenames, grading-rule pointers) from the tree and fails
# with a named reason for every divergence.
#
# Checks:
#   1. loader contract  — every *.lab.json has id == file stem and
#      id ~= ^lab[0-9]{2}_[a-z][a-z0-9_]*$, spec_version in {1,2}, a title,
#      only allowed top-level keys, and no v2-only key in a v1 document
#      (mirrors src/app/widgets/lab_spec_loader.cpp);
#   2. slot uniqueness  — no two canonical labs claim the same labNN number;
#   3. prerequisites    — entries are {path[,note]} objects, never bare strings
#      (a string prerequisite is a knowledge reference and must live in
#      prerequisite_knowledge[]);
#   4. alias resolution — every alias names exactly one canonical lab, and
#      every alias target exists;
#   5. pack parity      — after alias resolution, pack basenames and lab ids
#      are a bijection (grading_corpus and friends declared non_lab_packs);
#   6. declared files   — grading_rules / pipeline / data_spec paths of every
#      canonical entry exist and parse as JSON;
#   7. --strict-data    — additionally require every *.labspec.json to have a
#      merged canonical counterpart (see scripts/upgrade_labspec.py).
#
# Exit 0 iff consistent. Exit 1 with a reason per violation otherwise.

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABS = os.path.join(ROOT, "data", "labs")
PACKS = os.path.join(LABS, "packs")
REGISTRY = os.path.join(LABS, "lab-registry.json")

ID_RE = re.compile(r"^lab[0-9]{2}_[a-z][a-z0-9_]*$")
NUM_RE = re.compile(r"^lab([0-9]{2})_")

ALLOWED_ROOT_KEYS = {
    "spec_version", "id", "title", "title_zh", "objective", "prerequisites",
    "steps", "grading_ref", "thinking_questions",
    "objective_zh", "glossary", "expected_artifacts", "param_ranges",
    "grading_rules", "principles", "prerequisite_knowledge",
}
V2_ONLY_KEYS = {
    "objective_zh", "glossary", "expected_artifacts", "param_ranges",
    "grading_rules", "principles", "prerequisite_knowledge",
}


class Violations:
    def __init__(self):
        self.items = []

    def add(self, where, reason):
        self.items.append({"where": where, "reason": reason})

    def __bool__(self):
        return bool(self.items)


def load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def lab_documents():
    """(name, doc) for every lab document in data/labs (non-recursive)."""
    out = []
    for name in sorted(os.listdir(LABS)):
        if name.endswith(".lab.json") or name.endswith(".labspec.json"):
            out.append((name, load_json(os.path.join(LABS, name))))
    return out


def check_loader_contract(v):
    for name, doc in lab_documents():
        if not name.endswith(".lab.json"):
            continue  # *.labspec.json is the D3 authoring format; check 7 covers it
        where = "data/labs/" + name
        if not isinstance(doc, dict):
            v.add(where, "root must be an object")
            continue

        version = doc.get("spec_version")
        if version not in (1, 2):
            v.add(where, "spec_version must be 1 or 2 (got %r)" % (version,))
            version = None
        if version == 1:
            for key in sorted(V2_ONLY_KEYS & set(doc)):
                v.add(where, "top-level key '%s' requires spec_version 2" % key)

        for key in sorted(set(doc) - ALLOWED_ROOT_KEYS):
            v.add(where, "unknown top-level key '%s'" % key)

        lab_id = doc.get("id")
        if not isinstance(lab_id, str) or not lab_id:
            v.add(where, "id must be a non-empty string")
            continue
        if not ID_RE.match(lab_id):
            v.add(where, "id '%s' does not match ^lab[0-9]{2}_[a-z][a-z0-9_]*$" % lab_id)
        stem = name[: -len(".lab.json")]
        if stem != lab_id:
            v.add(where, "id '%s' does not equal its file stem '%s'" % (lab_id, stem))
        if not doc.get("title"):
            v.add(where, "title must be a non-empty string")

        prereqs = doc.get("prerequisites")
        if isinstance(prereqs, list):
            for entry in prereqs:
                if isinstance(entry, str):
                    v.add(where, "prerequisites entry %r is a bare string; knowledge "
                                 "prerequisites belong in prerequisite_knowledge[]" % entry)
                elif not isinstance(entry, dict) or "path" not in entry:
                    v.add(where, "prerequisites entry must be an object with 'path'")
                elif set(entry) - {"path", "note"}:
                    v.add(where, "prerequisites entry has unknown keys %s"
                          % sorted(set(entry) - {"path", "note"}))


def check_slot_uniqueness(v, doc_ids):
    """Only the strict-loader domain (the canonical `labNN_` ids) is checked.

    A `*.labspec.json` id is the legacy D3 vocabulary by construction — it is
    an alias, not a slot claim — so it is validated by check_legacy_vocabulary
    instead of here.
    """
    slots = {}
    for lab_id in sorted(doc_ids):
        m = NUM_RE.match(lab_id)
        if not m:
            v.add(lab_id, "id does not start with a two-digit lab number")
            continue
        slots.setdefault(m.group(1), []).append(lab_id)
    for number, ids in sorted(slots.items()):
        if len(ids) > 1:
            v.add("lab%s" % number, "lab number %s is claimed by %s" % (number, ", ".join(ids)))


def check_aliases(v, registry, doc_ids):
    canonical = registry.get("canonical", {})
    for lab_id in sorted(canonical):
        if lab_id not in doc_ids:
            v.add(lab_id, "canonical id has no lab document in data/labs/")
        entry = canonical[lab_id]
        for alias in entry.get("aliases", []):
            if alias == lab_id:
                v.add(lab_id, "alias '%s' is the canonical id itself" % alias)
        for key in ("grading_rules", "pipeline", "data_spec"):
            path = entry.get(key)
            if not path:
                continue
            full = os.path.join(ROOT, path.replace("/", os.sep))
            if not os.path.isfile(full):
                v.add(lab_id, "%s points at missing file %s" % (key, path))
                continue
            try:
                load_json(full)
            except ValueError as exc:
                v.add(lab_id, "%s is not valid JSON (%s)" % (path, exc))

    seen = {}
    for lab_id in sorted(canonical):
        for alias in canonical[lab_id].get("aliases", []):
            if alias in seen:
                v.add(alias, "alias declared by both %s and %s" % (seen[alias], lab_id))
            seen[alias] = lab_id

    alias_packs = registry.get("alias_packs", {})
    for pack, target in sorted(alias_packs.items()):
        if target not in doc_ids:
            v.add(pack, "alias pack resolves to '%s', which is not a lab id" % target)


def check_legacy_vocabulary(v, registry, documents):
    """Every D3 `*.labspec.json` id must be an alias of a canonical lab, or be
    explicitly declared out of scope (another track owns it)."""
    out_of_scope = set(registry.get("out_of_scope", {}))
    aliases = {a for entry in registry.get("canonical", {}).values()
               for a in entry.get("aliases", [])}
    for name, doc in documents:
        if not name.endswith(".labspec.json"):
            continue
        stem = name[: -len(".labspec.json")]
        lab_id = doc.get("id")
        if stem in out_of_scope:
            continue
        if lab_id not in aliases:
            v.add("data/labs/" + name,
                  "labspec id '%s' is neither an alias of a canonical lab nor "
                  "declared out_of_scope" % lab_id)


def check_pack_parity(v, registry, doc_ids):
    alias_packs = registry.get("alias_packs", {})
    non_lab = set(registry.get("non_lab_packs", {}))

    packs = set()
    for name in sorted(os.listdir(PACKS)):
        if not name.endswith(".pack.json"):
            continue
        base = name[: -len(".pack.json")]
        if base in non_lab:
            continue
        packs.add(alias_packs.get(base, base))

    # Only *canonical* lab ids need their own pack. A labspec id that is
    # registered as an alias is the same lab under its legacy name, and its
    # pack is resolved through alias_packs — otherwise every alias would look
    # like a pack-less lab.
    # alias_packs maps pack basename -> canonical id, so its KEYS are the
    # legacy spellings; the values are canonical and keep needing a pack.
    alias_ids = set(alias_packs.keys()) | {
        a for entry in registry.get("canonical", {}).values()
        for a in entry.get("aliases", [])}
    canonical_lab_ids = {i for i in doc_ids if i not in alias_ids}

    missing = sorted(canonical_lab_ids - packs)
    orphan = sorted(packs - canonical_lab_ids)
    for lab_id in missing:
        v.add(lab_id, "lab has no pack under data/labs/packs/")
    for pack in orphan:
        v.add(pack, "pack names no known lab (declare it in alias_packs or non_lab_packs)")


def check_labspec_counterparts(v, registry):
    """--strict-data: every D3 authoring file must have a merged canonical
    counterpart, except the ones another track owns."""
    out_of_scope = set(registry.get("out_of_scope", {}))
    for name in sorted(os.listdir(LABS)):
        if not name.endswith(".labspec.json"):
            continue
        stem = name[: -len(".labspec.json")]
        if stem in out_of_scope:
            continue
        target = None
        for lab_id, entry in registry.get("canonical", {}).items():
            if entry.get("source", "").endswith(name):
                target = lab_id
        if target is None:
            v.add("data/labs/" + name,
                  "labspec has no canonical entry in lab-registry.json")
            continue
        if not os.path.isfile(os.path.join(LABS, target + ".lab.json")):
            v.add("data/labs/" + name,
                  "labspec has no merged counterpart %s.lab.json" % target)


def main():
    parser = argparse.ArgumentParser(
        description="enforce data/labs/lab-registry.json against the tree")
    parser.add_argument("--strict-data", action="store_true",
                        help="also require every labspec to have a merged canonical counterpart")
    parser.add_argument("--json", action="store_true",
                        help="print violations as JSON")
    args = parser.parse_args()

    if not os.path.isfile(REGISTRY):
        print("check_lab_registry: missing %s" % rel(REGISTRY))
        return 1
    registry = load_json(REGISTRY)

    v = Violations()
    documents = lab_documents()
    doc_ids = {doc.get("id") for _, doc in documents if isinstance(doc, dict)}
    canonical_ids = {doc.get("id") for name, doc in documents
                     if isinstance(doc, dict) and name.endswith(".lab.json")}

    check_loader_contract(v)
    check_slot_uniqueness(v, canonical_ids)
    check_legacy_vocabulary(v, registry, documents)
    check_aliases(v, registry, doc_ids)
    check_pack_parity(v, registry, doc_ids)
    if args.strict_data:
        check_labspec_counterparts(v, registry)

    if v:
        if args.json:
            print(json.dumps({"ok": False, "violations": v.items},
                             ensure_ascii=False, indent=2))
        else:
            print("check_lab_registry: FAILED (%d violation(s))" % len(v.items))
            for item in v.items:
                print("  %s: %s" % (item["where"], item["reason"]))
        return 1

    print("check_lab_registry: ok (%d lab ids, %d canonical, %d aliases)"
          % (len(doc_ids), len(registry.get("canonical", {})),
             len(registry.get("alias_packs", {}))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
