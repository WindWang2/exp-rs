#!/usr/bin/env python3
"""
snapshot_diff_ref.py — independent reference implementation of the drift
report defined in src/contracts/snapshot_diff.h.

Why this exists: src/contracts/snapshot_diff.cpp is *code I wrote*, so a test
that calls it and asserts against its own output proves nothing about whether
the classification rules are right. This reference is written from the
specification in the header (identity = (kind,id) for nodes, (kind,from,to)
for edges, operatorId for census entries; `origin` is payload), independently
of the C++ control flow. Agreement between the two, on the real committed
snapshot versus a live generation, is the evidence that the rules are
implemented as specified.

Also used to produce the attribution table for the PR body.
"""
from __future__ import annotations

import json
import sys
from collections import OrderedDict

GRAPH_SCHEMA = "exp.contract.graph.v1"
CENSUS_SCHEMA = "exp.determinism_census.v1"

CHANGE_RANK = {"added": 0, "removed": 1, "changed": 2}


def _s(obj, key):
    v = obj.get(key, "")
    if isinstance(v, str):
        return v
    if v is None:
        return ""
    return json.dumps(v, ensure_ascii=False, separators=(",", ":"))


def _index_nodes(nodes):
    """identity (kind,id) -> (kind, readable_id, origin)

    The readable id is the bare node id: the report already carries `kind` in
    its own column, so prefixing it would print e.g.
    `capability_entry capability_entry/benchmark:compare`.
    """
    out = OrderedDict()
    for n in nodes:
        key = (_s(n, "kind"), _s(n, "id"))
        out.setdefault(key, (_s(n, "kind"), _s(n, "id"), _s(n, "origin")))
    return out


def _index_edges(edges):
    """identity (kind,from,to) -> (kind, readable_id, origin)"""
    out = OrderedDict()
    for e in edges:
        key = (_s(e, "kind"), _s(e, "from"), _s(e, "to"))
        readable = f"{_s(e, 'kind')} {_s(e, 'from')} -> {_s(e, 'to')}"
        out.setdefault(key, ("edge", readable, _s(e, "origin")))
    return out


def _index_entries(entries):
    """identity operatorId -> (kind, operatorId, origin)"""
    out = OrderedDict()
    for e in entries:
        key = _s(e, "operatorId")
        out.setdefault(key, ("entry", key, _s(e, "origin")))
    return out


def _diff_sets(rec, live, lines):
    for key, (kind, rid, origin) in rec.items():
        cur = live.get(key)
        if cur is None:
            lines.append({"change": "removed", "kind": kind, "id": rid})
        elif cur[2] != origin:
            lines.append({"change": "changed", "kind": kind, "id": rid,
                          "field": "origin", "before": origin, "after": cur[2]})
    for key, (kind, rid, _origin) in live.items():
        if key not in rec:
            lines.append({"change": "added", "kind": kind, "id": rid})


def diff(recorded, live):
    """Return dict(schema, schemaMismatch, lines[]) — mirrors the C++ API."""
    rs, ls = _s(recorded, "schema"), _s(live, "schema")
    if not rs:
        raise ValueError("recorded document carries no schema member")
    if not ls:
        raise ValueError("live document carries no schema member")
    if rs != ls:
        return {"schema": rs, "schemaMismatch": True, "lines": []}

    lines = []
    if rs == GRAPH_SCHEMA:
        if not all(k in recorded for k in ("nodes", "edges")) or \
           not all(k in live for k in ("nodes", "edges")):
            raise ValueError(f"document does not match schema {rs}")
        _diff_sets(_index_nodes(recorded["nodes"]),
                   _index_nodes(live["nodes"]), lines)
        _diff_sets(_index_edges(recorded["edges"]),
                   _index_edges(live["edges"]), lines)
    elif rs == CENSUS_SCHEMA:
        if "entries" not in recorded or "entries" not in live:
            raise ValueError(f"document does not match schema {rs}")
        _diff_sets(_index_entries(recorded["entries"]),
                   _index_entries(live["entries"]), lines)
    else:
        raise ValueError(f"unrecognized snapshot schema: {rs}")

    lines.sort(key=lambda l: (CHANGE_RANK.get(l["change"], 9),
                              l.get("kind", ""), l.get("id", "")))
    return {"schema": rs, "schemaMismatch": False, "lines": lines}


def format_report(rep):
    out = [f"snapshot schema: {rep['schema']}"]
    if rep["schemaMismatch"]:
        out.append("!! SCHEMA MISMATCH — regenerate and review.")
        return "\n".join(out)
    if not rep["lines"]:
        out.append("no element differences")
        return "\n".join(out)
    add = sum(1 for l in rep["lines"] if l["change"] == "added")
    rem = sum(1 for l in rep["lines"] if l["change"] == "removed")
    chg = sum(1 for l in rep["lines"] if l["change"] == "changed")
    out.append(f"differences: {len(rep['lines'])} (+{add} -{rem} ~{chg})")
    for l in rep["lines"]:
        if l["change"] == "added":
            out.append(f"  + {l['kind']} {l['id']}")
        elif l["change"] == "removed":
            out.append(f"  - {l['kind']} {l['id']}")
        else:
            out.append(f"  ~ {l['kind']} {l['id']} {l['field']}: "
                       f"`{l['before']}` -> `{l['after']}`")
    return "\n".join(out)


def main(argv):
    if len(argv) != 3:
        print("usage: snapshot_diff_ref.py <recorded.json> <live.json>")
        return 2
    with open(argv[1], encoding="utf-8") as f:
        recorded = json.load(f)
    with open(argv[2], encoding="utf-8") as f:
        live = json.load(f)
    rep = diff(recorded, live)
    print(format_report(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
