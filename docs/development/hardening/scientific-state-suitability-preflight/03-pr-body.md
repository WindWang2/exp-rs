# fix(scientific-state): hardening 10/20 — fact-chain integrity across passport, suitability and the preflight gate

Hardening track `scientific-state-suitability-preflight` against master
`a9dc33fa73` (post-#1236). Scope: existing modules only — no new product
directions, no second fact authority. **Online CI not awaited; PR left
unmerged by design.**

## Verification note (operator directive)

Per the host operator's mid-run instruction, **no local build or test
execution was performed** in this slice. Every regression oracle below
documents its RED side as a source-level trace on master (file:line, why
the old code cannot satisfy the assertion); CI provides the first green
run. Static verification replaced the local build loop: implementation
self-review plus an independent adversarial review that additionally
re-validated all five modified translation units with `-fsyntax-only`
under the project's real compile flags (`build-dev/compile_commands.json`)
— all clean — and traced every verdict-flip vector against the existing
suites (none flip; see `02-test-ledger.md`). Full evidence:
`docs/development/hardening/scientific-state-suitability-preflight/02-test-ledger.md`.

## Independent review gate

The adversarial reviewer returned **PROCEED-WITH-FIXES**; findings: 4×
PASS (using-declarations, control flow, `<cmath>`, verdict-flip matrix,
autonomy-test traces) and one FAIL that is the dedup dependency itself —
`test_preflight_report_schema` stays a build-graph orphan on this branch
until **#1246** lands (its registration intentionally lives there after
the dedup). Merge order: #1246 first, or this PR immediately after it.
The reviewer's P3 (stale `skip_preflight` doc in
`docs/agent/scientific-preflight.md`) is fixed in this branch.

## What was broken and what this fixes

### 0. Dedup: `src/preflight` wiring belongs to #1246
This recon independently found that #1207's `sicnu.preflight.report/1`
module is dead code (no `add_subdirectory`, its schema test unregistered).
Open PR **#1246** (`hardening/integration-build-contract-drift`) fixes
exactly that — same `add_subdirectory`, the same test registered, plus a
wiring drift oracle — so this branch **dropped its own identical wiring
commit** and leaves the shared central files to #1246 (conflict-gate
rule: no duplicate implementations on shared files).

### 1. Grid facts were silently zero on the production input shape (P1)
`raster_inspect_tool` emits `size`/`pixelSize` as JSON **objects**;
`gridFacts` parsed only **arrays**, so on every inspect-derived
understanding document the size/resolution comparisons in
`opticalChangeRules`, `sarChangeRules`, `inferenceRules` and
`multimodalRules` no-oped. `gridFacts` now parses both shapes with numeric
guards (the GridShapeFacts review-A-4 convention already used in
`workflow_analysis.cpp`). The eval suites never fed `size`/`pixel_size` —
new cases do.

### 3. `skip_preflight=true` could override a blocked verdict (P1)
`harness:execute_plan` skipped `preflightIntent` entirely when the caller
set `skip_preflight`, so a plan whose inputs did not even resolve compiled
and submitted with `executed=true` — contradicting the
`scientific_preflight.h` contract ("blocked plans are refused; the LLM
cannot override a blocked verdict"). The gate is now unconditional for
typed intents; the flag is removed from the tool schema; intent-less
custom plans keep their natural bypass (no rule pack to run).

### 4. Non-finite doubles broke the passport's self-consistency (P1)
A NaN geotransform flowed into `pixel_size`, serialized as JSON `null`,
and the module's own `fromJson` then rejected the document it had just
produced; `1e+9999` parsed as +inf and re-serialized to a non-strict
token. `readDouble` now rejects non-finite values (typed InvalidField) and
the resolver grounds nothing on a non-finite geotransform (typed unknowns
+ `geometry.geotransform_not_finite` note).

### 5. Honest degradation gaps (P2)
- Declared-but-empty `temporal_facts` bypassed the phenology min-scene
  checks silently; it now degrades to the same TIME_ORDER_INVALID warning
  as absent facts, and non-string date entries no longer compare via
  `asString()`.
- `inferenceRules` compared a degree-based pixel size against
  `min/max_resolution_meters`, calling every EPSG:4326 scene "finer than
  the model's recommended minimum"; geographic CRS now degrades to a
  "cannot be verified" advice. The closed geographic-authid list is
  consolidated into `facts::isGeographicAuthid` (workflow_analysis
  delegates; CRS:84 added, matching `workflow_facts`).
- The `fixable` verdict was unreachable (`addWarning` hard-coded
  `repairable=false`); the resample warning opts in, which is what the
  session fixable→proposals flow keys on.
- The GDAL collector's metadata cap bounded *scanned* entries, so >512-item
  files were truncated with `dropped()==0` and no
  `facts.metadata_truncated` note — "truncation is never silent" failed
  exactly where it matters. The cap is `MetadataItems`' contract; the loop
  scans everything.
- `DatasetFacts::fromJson` read extent doubles with a silent 0.0 default;
  a `finiteDouble` gate applies the module's own Slice G standard
  (`suitability.facts_invalid`).

## Dedup

All open PRs re-checked by `gh pr diff --name-only` at branch time
(#1237–#1250). This diff touches none of the files owned by the feat PRs
(#1237–#1241), the sibling hardening PRs (#1242/#1243/#1244/#1245/#1247/
#1248/#1249/#1250), and — after the dedup above — none of #1246's files
either. 0 open issues. Old `agent/flash-*` / `rs14-unified-verifier`
branches were mined as clue sources only; nothing was ported. PR #1248
touches `plan_tools.cpp` and `test_autonomy_gate.cpp` in disjoint hunks
(role/domain credential gating and verification integrity vs. the
preflight gate flag removal); its new forged-role test passes identically
under the unconditional gate (its plan carries no inputs, and the
autonomy refusal precedes preflight).

## Test evidence (oracles, RED on master — see ledger for the traces)

- `test_platform5` — object-shape grid change cases; temporal/unit
  degradation cases (verdicts `blocked`/`fixable` + message assertions).
- `test_autonomy_gate` — `skip_preflight` no longer bypasses a blocked
  gate (`executed=false`, `preflight.verdict=="blocked"`).
- `test_scientific_state_review/geo/gdal` — non-finite rejection
  (parse + resolve side, including passport self-round-trip); cap-drop
  counting with a 600-item GTiff.
- `test_suitability_adversarial` — non-finite/missing extent members fail
  typed; sane facts keep parsing.

## Known limitations (P3, recorded in the ledger)

- `src/preflight` remains a schema leaf; the engine/ack/budget layer is
  #1207's declared slice B (future direction, not implemented here).
- `sarFacts` calibration substring match is fragile in principle
  (unreachable with today's vocabulary).
- The per-entry `supervised=false` opt-out is effectively dead; needs a
  product decision.
- Collector fixtures still write under `CMAKE_SOURCE_DIR/build-rs14-passport`
  (pre-existing pattern; follow-up hygiene).

## Rollback

Five conventional commits; revert range `736650d75a..HEAD`. The only
behavioral externally-visible changes are the unconditional execute_plan
gate and typed rejections of previously-silently-garbled inputs — both are
fail-closed tightenings pinned by tests.
