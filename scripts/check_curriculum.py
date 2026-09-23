#!/usr/bin/env python3
"""check_curriculum.py — light CI gate for sicnu.curriculum/1 manifests.

Checks (structural layer only — deliberately NOT duplicated here):
  * manifest parses and validates against data/schemas/curriculum.schema.json
  * every lab_id resolves through data/labs/<id>.lab.json, the
    lab-registry (canonical/alias), or the registry's out_of_scope map
  * every required_data_packs entry exists as data/labs/packs/<name>.pack.json
  * prerequisite_modules form a DAG among known module ids

Operator availability is a RUNTIME question (Processing Registry +
CapabilityKnowledge) and is answered by the C++ availability report
(src/agent/harness/curriculum_availability.*); this script never guesses it.

Exit codes: 0 consistent · 1 inconsistent · 2 usage error.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LAB_MODULE_ID = re.compile(r"^m[0-9]{2}_[a-z][a-z0-9_]*$")

LAB_KEYS = {"lab_id", "role", "estimated_effort_minutes", "required_data_packs", "teacher_notes"}
MODULE_KEYS = {
    "id", "index", "title", "title_zh", "summary_zh", "learning_outcomes",
    "prerequisite_modules", "estimated_effort_minutes", "optional", "labs",
}
TOP_KEYS = {"schema", "id", "title", "title_zh", "audience_zh", "note_zh",
            "modules", "forward_references"}
FORWARD_KEYS = {"capability", "reason_zh", "wiring"}
ROLES = {"core", "optional", "external"}


def fail(message: str) -> list[str]:
    return [message]


def check_manifest(path: Path) -> list[str]:
    problems: list[str] = []
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return fail(f"{path}: unreadable or invalid JSON: {exc}")

    schema = manifest.get("schema")
    if schema != "sicnu.curriculum/1":
        return fail(f"{path}: schema must be 'sicnu.curriculum/1', got {schema!r}")

    for key in manifest:
        if key not in TOP_KEYS:
            problems.append(f"{path}: unknown top-level key {key!r}")

    labs_dir = REPO / "data" / "labs"
    packs_dir = labs_dir / "packs"

    registry: dict = {}
    registry_path = labs_dir / "lab-registry.json"
    if registry_path.is_file():
        try:
            registry = json.loads(registry_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            return fail(f"{registry_path}: invalid JSON: {exc}")
    canonical = registry.get("canonical", {})
    out_of_scope = registry.get("out_of_scope", {})

    def resolve_lab(lab_id: str) -> str:
        doc = labs_dir / f"{lab_id}.lab.json"
        if doc.is_file():
            try:
                parsed = json.loads(doc.read_text(encoding="utf-8"))
                # 1|2|3 mirrors lab_spec_loader.cpp / curriculum_catalog.cpp:
                # spec_version 3 (runtime generation, ADR 0174) is a legal
                # labspec, not an unknown reference.
                if parsed.get("id") == lab_id and parsed.get("spec_version") in (1, 2, 3):
                    return "labspec"
            except json.JSONDecodeError:
                pass
        for entry_id, entry in canonical.items():
            aliases = entry.get("aliases", []) if isinstance(entry, dict) else []
            if entry_id == lab_id or lab_id in aliases:
                source = entry.get("source", "")
                if source and (labs_dir / Path(source).name).is_file():
                    return "registry"
        if isinstance(out_of_scope, dict) and lab_id in out_of_scope:
            return "external"
        return "unknown"

    modules = manifest.get("modules")
    if not isinstance(modules, list) or not modules:
        return fail(f"{path}: modules must be a non-empty array")

    module_ids: set[str] = set()
    for module in modules:
        if isinstance(module, dict) and isinstance(module.get("id"), str):
            module_ids.add(module["id"])

    seen_ids: set[str] = set()
    seen_indexes: set[int] = set()
    edges: dict[str, list[str]] = {mid: [] for mid in module_ids}
    indegree: dict[str, int] = {mid: 0 for mid in module_ids}

    for position, module in enumerate(modules, start=1):
        at = f"modules[{position - 1}]"
        if not isinstance(module, dict):
            problems.append(f"{path}: {at} must be an object")
            continue
        for key in module:
            if key not in MODULE_KEYS:
                problems.append(f"{path}: {at}.{key}: unknown key")
        module_id = module.get("id", "")
        if not isinstance(module_id, str) or not LAB_MODULE_ID.match(module_id):
            problems.append(f"{path}: {at}.id {module_id!r} does not match {LAB_MODULE_ID.pattern}")
        elif module_id in seen_ids:
            problems.append(f"{path}: {at}.id: duplicate module id {module_id!r}")
        seen_ids.add(module_id)

        index = module.get("index")
        if not isinstance(index, int) or index < 1:
            problems.append(f"{path}: {at}.index must be an integer >= 1")
        elif index in seen_indexes:
            problems.append(f"{path}: {at}.index: duplicate index {index}")
        seen_indexes.add(index)

        for field in ("title", "title_zh", "summary_zh"):
            if not isinstance(module.get(field), str) or not module.get(field):
                problems.append(f"{path}: {at}.{field}: must be a non-empty string")

        outcomes = module.get("learning_outcomes")
        if not isinstance(outcomes, list) or not outcomes:
            problems.append(f"{path}: {at}.learning_outcomes: must be a non-empty array")
        elif not all(isinstance(o, str) and o for o in outcomes):
            problems.append(f"{path}: {at}.learning_outcomes: entries must be non-empty strings")

        prereqs = module.get("prerequisite_modules", [])
        if not isinstance(prereqs, list):
            problems.append(f"{path}: {at}.prerequisite_modules: must be an array")
            prereqs = []
        for prereq in prereqs:
            if prereq not in module_ids:
                problems.append(
                    f"{path}: {at}.prerequisite_modules: unknown module {prereq!r}")
            elif module_id in edges:
                edges[prereq].append(module_id)
                indegree[module_id] += 1

        effort = module.get("estimated_effort_minutes")
        if effort is not None and (not isinstance(effort, int) or effort < 1):
            problems.append(f"{path}: {at}.estimated_effort_minutes: must be an integer >= 1")

        labs = module.get("labs")
        if not isinstance(labs, list) or not labs:
            problems.append(f"{path}: {at}.labs: must be a non-empty array")
            continue
        for lab_position, lab in enumerate(labs, start=1):
            lab_at = f"{at}.labs[{lab_position - 1}]"
            if not isinstance(lab, dict):
                problems.append(f"{path}: {lab_at} must be an object")
                continue
            for key in lab:
                if key not in LAB_KEYS:
                    problems.append(f"{path}: {lab_at}.{key}: unknown key")
            lab_id = lab.get("lab_id", "")
            role = lab.get("role", "")
            if role not in ROLES:
                problems.append(f"{path}: {lab_at}.role: must be one of {sorted(ROLES)}")
            if not isinstance(lab_id, str) or not lab_id:
                problems.append(f"{path}: {lab_at}.lab_id: must be a non-empty string")
                continue
            resolution = resolve_lab(lab_id)
            if role in ("core", "optional") and resolution not in ("labspec", "registry"):
                problems.append(
                    f"{path}: {lab_at}.lab_id: lab {lab_id!r} does not resolve "
                    f"(got {resolution!r})")
            if role == "external" and (
                not isinstance(out_of_scope, dict) or lab_id not in out_of_scope
            ):
                problems.append(
                    f"{path}: {lab_at}.lab_id: external lab {lab_id!r} is not declared "
                    "in lab-registry out_of_scope")
            lab_effort = lab.get("estimated_effort_minutes")
            if not isinstance(lab_effort, int) or lab_effort < 1:
                problems.append(
                    f"{path}: {lab_at}.estimated_effort_minutes: must be an integer >= 1")
            packs = lab.get("required_data_packs", [])
            if not isinstance(packs, list):
                problems.append(f"{path}: {lab_at}.required_data_packs: must be an array")
                packs = []
            for pack in packs:
                pack_path = packs_dir / f"{pack}.pack.json"
                ok = False
                if pack_path.is_file():
                    try:
                        parsed = json.loads(pack_path.read_text(encoding="utf-8"))
                        ok = parsed.get("schema_version") == "sicnu.lab-pack/1"
                    except json.JSONDecodeError:
                        ok = False
                if not ok:
                    problems.append(
                        f"{path}: {lab_at}.required_data_packs: pack {pack!r} missing "
                        f"or not sicnu.lab-pack/1 ({pack_path})")
            notes = lab.get("teacher_notes", {})
            if notes and not isinstance(notes, dict):
                problems.append(f"{path}: {lab_at}.teacher_notes: must be an object")

    for forward in manifest.get("forward_references", []) or []:
        if not isinstance(forward, dict):
            problems.append(f"{path}: forward_references entries must be objects")
            continue
        for key in forward:
            if key not in FORWARD_KEYS:
                problems.append(f"{path}: forward_references.{key}: unknown key")
        if not isinstance(forward.get("capability"), str) or not forward.get("capability"):
            problems.append(f"{path}: forward_references.capability: must be a non-empty string")
        if not isinstance(forward.get("reason_zh"), str) or not forward.get("reason_zh"):
            problems.append(
                f"{path}: forward_references.reason_zh: a forward reference must state "
                "why the capability is not promised")

    # Kahn — prerequisites must form a DAG.
    ready = [mid for mid, degree in indegree.items() if degree == 0]
    visited = 0
    while ready:
        current = ready.pop()
        visited += 1
        for nxt in edges[current]:
            indegree[nxt] -= 1
            if indegree[nxt] == 0:
                ready.append(nxt)
    if visited != len(module_ids):
        problems.append(f"{path}: prerequisite_modules contains a cycle")

    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--manifest", action="append", default=[],
        help="manifest path (default: every data/curriculum/*.curriculum.json)")
    parser.add_argument("--json", action="store_true", help="machine-readable report")
    args = parser.parse_args(argv)

    if args.manifest:
        targets = [Path(p) for p in args.manifest]
    else:
        targets = sorted((REPO / "data" / "curriculum").glob("*.curriculum.json"))
        if not targets:
            print("check_curriculum: no manifests found under data/curriculum", file=sys.stderr)
            return 1

    problems: list[str] = []
    for target in targets:
        problems.extend(check_manifest(target))

    if args.json:
        print(json.dumps(
            {"schema": "sicnu.curriculum.check/1", "ok": not problems,
             "problems": problems}, ensure_ascii=False, indent=2))
    else:
        for problem in problems:
            print(f"CURRICULUM {problem}")
        print(f"check_curriculum: {'OK' if not problems else 'FAILED'} "
              f"({len(targets)} manifest(s), {len(problems)} problem(s))")
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
