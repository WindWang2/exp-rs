# PR BODY

## Summary

Track D1 (`ds41-capability-help-sync`): turn the duplicated manual maintenance
of registry → algorithm_meta → capability → help → CLI/MCP discovery into a
verifiable projection/generation + drift-gate discipline.

**Baseline finding (master `adf8f9895`): the capability/help metadata layer was
structurally broken on master** — 25 shipped JSON files were unparsable, both
D8/ADR-0122 guard tests were RED, and nothing compared the CLI/MCP/help
capability surfaces against the live registry. This PR repairs the damage with
the repository's own generators and closes the surface-parity/completeness
gaps with two new gates.

## Baseline

- `origin/master` @ `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`
- Local evidence only (execution environment had no GitHub access): PR/issue
  queries unavailable; dedup done against local refs + merged-track planning
  docs (see `.planning/ds41-capability-help-sync/DEDUP.md`).
- Red gates on master (prebuilt binaries, `QT_QPA_PLATFORM=offscreen`):
  - `test_algorithm_meta_drift`: FAIL — `expectedCatalog.size() == 43` vs
    live 53 task-declaring descriptors; 4 Layer-A sidecars truncated.
  - `test_capability_knowledge`: 12 cases / 8 FAILED — 19/138 v2 sidecars were
    unparsable duplicates from the bad merge `43dcf19cd` (PR #1022), ≥6
    registered operators had no sidecar, and the Layer-C knowledge mirror
    (`preprocess.json`) failed to load.
  - (Environment note: `test_surface_parity`'s CLI-subprocess case also fails
    on Windows debug because OpenCV writes INFO banners to stdout; unrelated
    to this Track — the new gates pin `OPENCV_LOG_LEVEL=warning` for their own
    subprocesses.)

## Scope

In scope:
- Repair of merge/edit-corrupted metadata (data/processing/**, data/help,
  data/agent/capabilities) to their last valid content (evidence: git parent
  comparison + parse verification; no semantic redesign).
- Regeneration of Layer-A sidecars (`sicnu_geo_rs_cli --export-catalog`) and
  Layer-B sidecars + pi/knowledge pages (`capability_knowledge_tool
  gen-meta`/`gen-pages`), with idempotency evidence.
- Authored-content completion for the 4 sidecars that shipped empty
  summary/failure_modes — grounded strictly in declared operator metadata and
  verified throw sites (no invented science).
- New gates: `tests/test_capability_surface_parity.cpp`,
  `tests/test_capability_completeness.cpp`.
- Factual pin refresh in `test_algorithm_meta_drift.cpp` (43 → measured),
  ADR reference fixes (D8 → ADR 0154, CN products → ADR 0157).

Out of scope (recorded, not fixed):
- Operator implementations / algorithm semantics (untouched).
- CLI/MCP projection key vocabularies and the `search_algorithms`
  documented-but-unimplemented tag/purpose filters (production surface
  behavior; owned by the merged `cli-mcp-agent-surface-11` track — its
  known-limitation notes).
- Units / NoData structured contracts (`rs_schema.h` has no unit field;
  infrastructure gap) — measured as a WARN census in the completeness gate,
  not fabricated.
- The F1/HelpCenter/help-viewer consolidation (help-system tracks).

## Cross-track compile-break repair (please route to the workflow track)

`origin/master` @ `adf8f9895` does **not compile**: commit `61a8c9b0d` ("fix(workflow): unwedge resume/cancel state-machine dead ends", 2026-09-19 23:38, ancestor of HEAD) declared `const WorkflowDocument resumedDef = parsed.value();` and later `const WorkflowDocument &resumedDef = m_state->def;` in the same function body of `PipelineRunCoordinator::resumeFromCheckpoint` (MSVC C2373/C2530/C2143). `m_state->def` is assigned from `resumedDef` four lines above the second use, so the second declaration is a pure alias of identical content. This PR removes that one line (commit `8a95744b8`) because `sicnu_workflow` is a link dependency of the capability test targets and no gate could be built or verified while master was broken. If the workflow track's intended resolution differs (e.g. renaming), re-land theirs and drop this hunk — behavior is identical either way.

## Design

Authority map (`.planning/ds41-capability-help-sync/AUTHORITY_MAP.md`): the
live `AlgorithmDescriptor`/`AgentMetadata` (built from `RSOperator::metadata()`
+ `schema()`) is the single source of truth; sidecar A, sidecar B, help
topics, MCP and CLI are projections of it. The new surface-parity gate anchors
every projection to one id universe and asserts schema parity on key samples
(name/type/default/enum), with enumerated exemptions only.

Completeness contract: every first-class capability needs non-empty summary,
failure_modes and io.outputs; io.inputs emptiness only via the enumerated
collection-style exemption table (23 operators whose inputs are
array-of-string path parameters — verified against their live schemas),
equality-checked so new gaps fail deliberately.

## Test commands & results

(two consecutive runs, identical results; plus a four-way tamper cycle: T1 derived-field drift / T2 authored emptiness / T3 Layer-A byte drift / T4 deleted sidecar each fail the corresponding gate, restoring passes all four):
```
test_algorithm_meta_drift, test_capability_knowledge,
test_capability_completeness, test_capability_surface_parity
capability_knowledge_tool gen-meta/gen-pages --check  (idempotency)
sicnu_geo_rs_cli --export-catalog                      (Layer-A idempotency)
```

## Resource constraints

- `-j2` build cap (≤2), `QT_QPA_PLATFORM=offscreen`, targeted targets only,
  worktree build `build-cap/` reusing the installed vcpkg tree.
- No online CI waits; all evidence local.

## Branch-name deviation

The environment runs a fleet of parallel agent worktrees whose janitor
periodically deletes `agent/*` branch refs — this Track's branch was deleted
twice mid-run (recovered via dangling commits). The work branch is therefore
`track/ds41-capability-help-sync` (the requested
`agent/ds41-capability-help-sync` name is unusable in this environment);
`backup/ds41-capability-help-sync` mirrors every committed state.

## Known limitations

- CLI `--list`/`algorithms list` full-engine superset includes non-rs
  families; the gate pins the rs: slice exactly and requires every CLI rs: id
  to resolve live (superset relation, documented in the test).
- Units/NoData: no structured contract exists (`rs_schema.h` has no unit field); the completeness gate reports a WARN census (15/152 sidecars mention NoData semantics) instead of fabricating content.
- Regenerated Layer-B sidecars carry the jsoncpp writer's `"key" : ` trailing space; the drift gate's byte-equality IS that writer's contract, and the regeneration normalizes master's previously mixed corpus (some files hand-stripped) to one deterministic form. `git diff --check` therefore reports trailing-whitespace warnings on generated files only — accepted, documented, and re-verified idempotent.
- Layer-C (`data/agent/capabilities`) was repaired to its last valid content only; in-flight branch lines carry additional Layer-C entries (rs:radiometric_qa / rs:solar_geometry / rs:brdf_normalization / rs:quality_mosaic, …) that the Layer-C owner should re-land.
- `AlgorithmMetaStore::loadFromDirectory` keys entries by id, so a duplicate-id Layer-A file would be invisible to the parity gate; the drift test's file-count assertion is the backstop — keep both gates together.
- `search_algorithms` tag/purpose filter documentation gap (pre-existing,
  production surface) — not touched here.
- Layer-C (`data/agent/capabilities`) was repaired only to its last valid
  content; its owning track should re-land any intended enrichment through the
  normal review path.


## Review disposition

- Pass 1 (data-repair correctness, read-only): 7/7 PASS; findings R1/R2 (commit-message corrections: 7 duplicated ids, "emptied" not "truncated") and R3 (repeated INVALID_PARAMETER code across distinct causes in 3 sidecars — accepted; `errorCatalog()` aggregates by code and `manifestPage` surfaces both `when` conditions) recorded in `.planning/ds41-capability-help-sync/EVIDENCE.md`.
- Pass 2 (final diff incl. the two new test files, read-only; reviewer independently re-ran the four gates): **P0 = 0**. P1-1 (exemption reason text was wrong for ~31/39 rows — fixed to the verified common cause) and P1-2 (schema parity ignored `items.type` and the `required` set — signatures extended; both fixed with the gates re-verified green) are resolved in `2b5709386`. P2 items recorded in EVIDENCE.md.
