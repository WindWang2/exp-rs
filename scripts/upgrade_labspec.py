#!/usr/bin/env python3
# upgrade_labspec.py — LabSpec 1 → LabSpec 2 migration (lab platform 12.0).
#
# LabSpec 2 is a strict superset of v1: same required fields, seven new optional
# structured fields (objective_zh, principles, glossary, expected_artifacts,
# param_ranges, grading_rules) and `spec_version: 2`. The C++ loader accepts
# both versions and rejects v2-only keys in v1 documents (src/app/widgets/
# lab_spec_loader.cpp); this script moves the shipped data to v2:
#
#   1. every data/labs/*.lab.json gets spec_version bumped to 2 (text-level,
#      no reformatting);
#   2. the four D3-era `*.labspec.json` authoring files (lab8–lab11) are
#      merged into the matching `.lab.json`: their structured Chinese teaching
#      content finally lands in the format every consumer parses.
#      Mapping: objectives→objective_zh (plus theme/audience/duration line),
#      principles→principles, glossary{term,en,definition}→glossary
#      {term_zh,term,definition_zh}, expected_results→expected_artifacts,
#      questions{prompt,hint}→thinking_questions strings.
#      NOT migrated (recorded in the PR notes): D3 `operators[]`/
#      `dependencies[]`/`notes[]` are tooling metadata already covered by
#      packs and data-specs; `grading_ref.intent_ref` stays D3-only — the
#      rules-file pointer (`grading_rules`) is left unset where no
#      `sicnu.lab.rules/1` file is addressed by the lab id.
#
# usage:
#   python3 scripts/upgrade_labspec.py            # apply
#   python3 scripts/upgrade_labspec.py --check    # exit 0 iff already migrated
#   python3 scripts/upgrade_labspec.py --project-root /path

import argparse
import json
import os
import sys

D3_MERGES = {
    "lab8_temporal_analysis": "lab8_temporal_analysis.labspec.json",
    "lab9_sar_processing": "lab9_sar_processing.labspec.json",
    "lab10_hyperspectral_analysis": "lab10_hyperspectral_analysis.labspec.json",
    "lab11_cartographic_mapping": "lab11_cartographic_mapping.labspec.json",
}


def dumps(doc):
    """Canonical on-disk formatting for .lab.json (2-space, unescaped zh)."""
    return json.dumps(doc, ensure_ascii=False, indent=2) + "\n"


def merge_d3(lab_doc, d3_doc, stem):
    """Fold D3 authoring content into a LabSpec 2 document. Pure function.
    @p stem is the file stem — the loader requires id == stem."""
    doc = json.loads(json.dumps(lab_doc))  # deep copy, key order preserved
    doc["spec_version"] = 2
    doc["id"] = stem

    objective_lines = []
    theme = d3_doc.get("theme")
    audience = d3_doc.get("audience")
    duration = d3_doc.get("duration_minutes")
    if theme or audience or duration:
        bits = []
        if theme:
            bits.append("主题：%s" % theme)
        meta = []
        if audience:
            meta.append("受众：%s" % audience)
        if duration:
            meta.append("约 %s 分钟" % duration)
        if meta:
            bits.append("（%s）" % "，".join(meta))
        objective_lines.append("".join(bits))
    objective_lines.extend(d3_doc.get("objectives", []))
    if objective_lines:
        doc["objective_zh"] = "\n".join(objective_lines)

    # Loader conformance fixes for the v1 file (data/labs is the strict
    # loader's domain: id must match ^labNN_...$ and equal the file stem;
    # prerequisites entries must be {path, note} data refs):
    # Knowledge prerequisites written as plain strings are not data refs;
    # move them to the v2 prerequisite_knowledge[] list instead of failing
    # the loader (idempotent: the strings leave prerequisites[] on the first
    # pass and live on in their own field).
    prerequisites = doc.get("prerequisites")
    if isinstance(prerequisites, list) and prerequisites \
            and any(isinstance(p, str) for p in prerequisites):
        knowledge = [p for p in prerequisites if isinstance(p, str)]
        doc["prerequisites"] = [
            p for p in prerequisites if not isinstance(p, str)
        ]
        if not doc["prerequisites"]:
            del doc["prerequisites"]
        existing_knowledge = doc.setdefault("prerequisite_knowledge", [])
        for item in knowledge:
            if item not in existing_knowledge:
                existing_knowledge.append(item)
    principles = [
        {k: p[k] for k in ("heading", "body") if k in p}
        for p in d3_doc.get("principles", [])
    ]
    for principle, source in zip(principles, d3_doc.get("principles", [])):
        if source.get("formulas"):
            principle["formulas"] = list(source["formulas"])
    if principles:
        doc["principles"] = principles

    glossary = []
    for entry in d3_doc.get("glossary", []):
        glossary.append({
            "term": entry.get("en") or entry.get("term", ""),
            "term_zh": entry.get("term", ""),
            "definition_zh": entry.get("definition", ""),
        })
    if glossary:
        doc["glossary"] = glossary

    artifacts = []
    for result in d3_doc.get("expected_results", []):
        artifact = {
            "path": result.get("artifact", ""),
            "note_zh": result.get("claim", ""),
        }
        if result.get("tolerance_note"):
            artifact["note_zh"] += "；%s" % result["tolerance_note"]
        if artifact["path"].endswith(".tif"):
            artifact["kind"] = "raster"
        else:
            artifact["kind"] = "file"
        artifacts.append(artifact)
    if artifacts:
        doc["expected_artifacts"] = artifacts

    existing = set(doc.get("thinking_questions", []))
    for question in d3_doc.get("questions", []):
        text = question.get("prompt", "")
        if question.get("hint"):
            text += "\n提示：%s" % question["hint"]
        if text and text not in existing:
            doc.setdefault("thinking_questions", []).append(text)
            existing.add(text)
    return doc


def migrated_bytes(labs_dir, name):
    """The exact bytes the migrated file must have."""
    path = os.path.join(labs_dir, name)
    with open(path, "r", encoding="utf-8") as handle:
        raw = handle.read()
    doc = json.loads(raw)
    version = doc.get("spec_version")
    stem = name[: -len(".lab.json")]
    if stem in D3_MERGES:
        d3_path = os.path.join(labs_dir, D3_MERGES[stem])
        if not os.path.exists(d3_path):
            if version == 2:
                return raw
            if version != 1:
                raise SystemExit("upgrade_labspec: %s has spec_version %r"
                                 % (name, version))
            return raw.replace('"spec_version": 1', '"spec_version": 2', 1)
        with open(d3_path, "r", encoding="utf-8") as handle:
            d3_doc = json.load(handle)
        return dumps(merge_d3(doc, d3_doc, stem))
    if version == 2:
        return raw
    if version != 1:
        raise SystemExit("upgrade_labspec: %s has spec_version %r" % (name, version))
    bumped = raw.replace('"spec_version": 1', '"spec_version": 2', 1)
    # A differently formatted file would silently no-op the text replace and
    # make --check pass on an unmigrated spec — verify the parsed result.
    if json.loads(bumped).get("spec_version") != 2:
        raise SystemExit("upgrade_labspec: could not bump spec_version in %s"
                         % name)
    return bumped


def main():
    parser = argparse.ArgumentParser(description="Migrate LabSpec v1 data to v2.")
    parser.add_argument("--check", action="store_true",
                        help="verify the migrated state; exit 1 with a diff list otherwise")
    parser.add_argument("--project-root", default=None,
                        help="repo root (default: script's grandparent)")
    args = parser.parse_args()
    root = args.project_root or os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))
    labs_dir = os.path.join(root, "data", "labs")
    if not os.path.isdir(labs_dir):
        raise SystemExit("upgrade_labspec: no data/labs under %s" % root)

    pending = []
    for name in sorted(os.listdir(labs_dir)):
        if not name.endswith(".lab.json"):
            continue
        wanted = migrated_bytes(labs_dir, name)
        path = os.path.join(labs_dir, name)
        with open(path, "r", encoding="utf-8") as handle:
            current = handle.read()
        if wanted != current:
            pending.append(name)
        if not args.check and wanted != current:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(wanted)
            print("migrated %s" % name)

    if args.check:
        if pending:
            print("upgrade_labspec: --check FAILED, not migrated: %s"
                  % ", ".join(pending))
            return 1
        print("upgrade_labspec: --check ok (all lab specs at LabSpec 2)")
        return 0
    print("upgrade_labspec: %d file(s) migrated" % len(pending))
    return 0


if __name__ == "__main__":
    sys.exit(main())
