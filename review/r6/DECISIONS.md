# R6 — Registry / Contract / Corpus / Manifest Source-of-Truth Convergence — decision log

Branch: `hardening/r6-registry-contract-source-truth`, base: master @ 1e28de867 (#1414).
Census inputs: `census-operators.md`, `census-vocabulary.md`, `census-bench-corpus.md`,
`census-guidance.md`, `census-models.md` (same directory). This file records what was
done about each finding and why.

## WP-A — operator registration single truth

- Census: 189 registered ids across 5 families; macro-vs-explicit: rs/gdal/opencv/otb
  aligned; **io 13 macros / 17 explicit** (`io:catalog_search`, `io:cube_plan`,
  `io:cube_window`, `io:cache_prefetch` had no macros). No macro-only id anywhere → the
  #1398 MSVC dead-strip exposure class is closed on today's master.
- Decision: add the 4 missing io macros so every family is exact-parity
  (`io_operators_init.cpp`); the macro list mirrors the family for every tool that greps
  the macro shape (e.g. `determinism_census.cpp`).
- Gate: `tests/test_source_truth_gates.cpp` "Operator macro, explicit and runtime
  registration sets are identical per family" — parses the five init TUs and pins
  macro set == explicit set == live `RSOperatorRegistry::operatorNames()` per family.

## WP-B — vocabulary convergence (all decisions evidence-backed, no blind renames)

| Stale id | Replacement | Evidence |
|---|---|---|
| `rs:change_detect` (bench corpus only) | `rs:change_detection` | registered `rs:change_detection`; #1407 fixed every non-bench site |
| `rs:co_register` (bench corpus only) | `rs:align` | chain step = same-sensor grid conformance emitting `aligned.tif`; `rs:align` description is an exact match; `rs:register_images` is the heavier optical-SAR contract |
| `rs:train_classifier` | `rs:supervised_classification` | train+predict mode with `training`/`modelOut`; `method` enum includes `rf` (backend factory `rs_classifier_backend_factory.cpp`) |
| `rs:pan_sharpen` | `gdal:pansharpen` | appears only as a redundancy-probe decoy that must be REAL (a wandering agent must fail for the right reason); exact registered counterpart |
| `temporal:smooth` / `temporal:gap_fill` | `rs:temporal_smooth` / `rs:temporal_gap_fill` | no such `temporal:` tools exist (the prefix owns the 12 collection tools); the named operators are registered |
| `workflow_orchestrator_tool.cpp` demo vocabulary | sensor-conditional `rs:landsat_import`/`rs:sentinel2_import`/`rs:gaofen_import` (same partition as `sensorProfile()`); `rs:change_cva`; `rs:threshold_raster`; `rs:fusion_gram_schmidt`; `opencv:gaussian_blur` (kernelSize/sigma, real schema); `rs:supervised_classification` + `method:"rf"` | every compiled chain node must resolve; the classification chain already requires OpenCV via `rs:supervised_classification` |
| `workflow_repair_engine` CrsMismatch | `gdal:reproject {dstCrs, resampling}` | registered op; `dstCrs` is its real parameter name (`target_crs` never existed anywhere) |
| `workflow_repair_engine` ResolutionMismatch | `rs:resample {resolution, resampling}` | `rs:resample` takes one square-cell `resolution`; non-square expectations have no closed adapter (no action) |
| `workflow_repair_engine` DataTypeMismatch | **rule retired** (violation still reported, no auto-action) | NO registered operator performs dtype conversion (`io:translate`/foundation `TranslateOptions` have no dtype knob); emitting `rs:convert_dtype` produced workflows that could never execute — a guaranteed-later-failure, not a repair. Mirrors the DimensionMismatch "no closed rule" precedent |
| `workflow_cost_estimator` coefficient keys | re-keyed to the same registered ids (+ `io:reproject`/`io:warp`, 7 import ops) | closed table keyed on ids that never existed; unknown ops silently default to 1.0 |
| `otb_svm_classification_operator.cpp` workflowHint | points at `rs:supervised_classification` predict-only (`modelIn`) | `otb:image_classifier` is registered NOWHERE; only `otb:svm_classification` (train) exists |
| `rs:inference` prose (2 sites) | `rs:infer` | registered id; sidecars use `rs:infer` |
| `preset_catalog_widget.cpp` | Landsat import steps → `rs:landsat_import`; generic raster imports → `io:translate`; slope/hillshade → `rs:terrain_analysis` + `product` param | `gdal:import`/`gdal:slope`/`gdal:hillshade` are registered nowhere; `cartography:compose/preflight/export` are legitimate agent-tool surface ids (graph_assembly handles both vocabularies) — NOT drift, left alone |
| docs: `gdal:warp` (grid policy) | `io:warp` | registered warp operator; the QGIS-compat wrappers stay described as wrappers |
| docs: foundation-5 "remain future work" bullet | rewritten: the four ops are registered now | the roadmap prose was stale in the other direction |

Deliberately NOT touched (classified non-drift or out of scope):
- `docs/adr/**`, `docs/superpowers/specs/**` — historical records.
- `docs/experiments/debugging.md` — mirrors `test_experiment_debugger` fixtures (lab-declared step vocabulary).
- `data/processing/algorithm_meta` 40/12/5 keyset split — generator-conditional (verified non-drift, #1393).
- All synthetic test fixtures and the eval negative-id corpus.
- Repair-engine rule ids in ADR 0162 prose (historical record; the code + tests carry the new table).

## WP-C — bench corpus migration

- Generator SPEC edited (6 stale ids + step payloads `labels`→`training`+`method:rf`,
  `pair` unchanged), full pack regenerated, `suite.json.version` 1.0.0 → 1.1.0, digest
  pins updated in `tests/test_agentbench_corpus.cpp` in the same commit. No digest
  fixture was hand-edited; verdict-relevant invariants (counts/failure classes) are
  id-independent and re-verified by the end-to-end grading test.
- Gate: new corpus test "bench tool vocabulary resolves against the live operator
  registry" — every allowed/redundant/fault/trace-step tool id must resolve, with three
  documented exemptions (`harness:*`, `model:`/`map:` opaque bench-internal families,
  `rs:forbidden_augment`), the last of which is pinned to its ABSENCE from the registry.
- `data/agent/bench/README.md` documents the vocabulary contract.

## WP-D — guidance sidecars

- Canonical keyset derived from the loader (`guidance_store.cpp` closed 12-key allowlist,
  fail-closed) + schema; NOT from majority vote.
- 13 missing (file, key) pairs authored across the 6 lean files, in the corpus style.
  `role` stays absent everywhere (generic-corpus convention per README).
- Gate: `test_explain_guidance_coverage` now pins the full canonical optional keyset per
  file (`role` pinned to absent).

## WP-E — model manifests

- **4 manifests were disk-newer, not 2** (ssl-embedding-encoder and unet-buildings-s2
  carry `id`/`model_version`/`license`(/`source`) identity keys the generator never
  emitted; a blind regenerate would have regressed all four). Direction of every diff:
  disk newer than spec.
- Decision: the SPEC is the stale side — `gen_model_library.py` extended
  (`input_size`/`resize`/`detection`/`extra_identity`, swin 4-band MEAN_STD_RGB_NIR);
  regenerated; all 24 generator manifests now byte-identical to disk (verified; the
  regen is a no-op).
- `models/sam-building` + `models/yolo-buildings` (v1-era, disk-only, zero
  code/test references; tests build their own temp manifests): **kept and
  marked legacy** (superseded by the generator-produced `sam-buildings-hr` /
  `yolo-building-detection`). Reversed the initial delete: removing shipped
  catalog entries is a user-visible catalog change (the catalog resolves by
  directory name), while `gen_model_library.py --check` only compares the
  manifests the generator PRODUCES, so keeping them costs no parity drift.
  `models/README.md` carries the legacy note; new work uses the equivalents.
- Gate: `gen_model_library.py --check` + ctest `test_model_library_generator_parity`
  (skipped where no interpreter).

## WP-F — STAC dual client (subagent-implemented, orchestrator-reviewed)

- `src/geospatial/stac/stac_client.*` is now the single data/transport authority
  (request building, egress/SSRF policy, pagination continuations, timeouts, detached
  fetch worker); `src/app/stac_client.*` is a thin Qt adapter keeping its public
  Qt-facing API (dialog churns zero) and the late-arrival landing policy. No third
  abstraction.
- Known, accepted deltas (from CPL being the single HTTP implementation, ADR 0139):
  hop-by-hop redirect re-validation cannot be reproduced; network-failure prose differs
  (typed domain messages instead of QNAM strings); transport budget carried over
  verbatim (10 s / 5 s connect / 1 attempt / egress policy on).
- Reviewer-noted accepted deltas (same class, recorded for the merge):
  (a) whole-page Item validation — every feature now passes `StacItem::parse`
  (type=Feature, id, properties.datetime, >=1 asset), so one nonconformant item fails
  the page with a typed error where the QNAM client delivered raw unvalidated docs;
  (b) the adapter refuses non-numeric bbox entries locally (typed, earlier) instead of
  passing the raw comma-join to the server; (c) same-generation detached fetches can
  land out of order (last-writer-wins on the continuation cache, as before).
- Test wiring: `test_stac_client` and `test_m2_batch_b_dialogs` link `Sicnu::Geospatial`
  (Qt6::Network no longer needed by either).

## WP-G — error contract convergence

- Internal single source: `harness_error.cpp` `kEntries` already existed; the two
  surface alias tables that lived OUTSIDE it (drift-prone) moved in:
  `cliExitCodeForSessionRefusal` (was an if-chain in cli_agent_ops_commands.cpp) and
  `mcpToolCodeForLegacy` (was an inline alias table in mcp_server.cpp), plus the
  taxonomy→published-exit-code projection `cliExitCodeForCode`.
- External contracts unchanged: CLI still prints `E-n:SYMBOL` + `CliErrorDetails`
  (same numbers, pinned by test_cli_agent_ops + new test_harness_error legs); MCP/agent
  envelope shape and values unchanged (pinned by test_mcp_server). No version bump
  needed — nothing observable moved.

## WP-H — WHOLE_ARCHIVE stopgaps

All four test-side `WHOLE_ARCHIVE,sicnu_agent_loop` links **retired with per-site
evidence**. Root cause of redundancy: #1335/#1366 gave `sicnu_agent` (SHARED) a PUBLIC
`sicnu_agent_loop` link edge, so the loop's PIC objects are embedded in and exported by
`libsicnu_agent.so`; the test-side whole-archive flags predate that edge and only
force-pulled an already-contained archive (`SicnuSharedLinkGuard` runs `--no-undefined`
on the shared libs, which is what guarantees their self-containment).

Experiment per site (remove → relink → run green, zero undefined refs at link or load):
1. `sicnu_add_test` helper (all 222 helper targets share the link recipe): relinked
   `test_capability_drift` (748 assertions green), `test_source_truth_gates`,
   `test_explain_guidance_coverage`, `test_verifier_robustness_r4`. The 12 test TUs
   that include agent_loop headers directly are wired via plain `add_executable`
   targets that link the archive explicitly — none use the helper.
2. `test_tool_call_dispatcher` — relinked, 226 assertions green.
3. `test_output_verifier` — relinked, 40 assertions green.
4. `test_llm_streaming_client` — relinked, 74 assertions green.

A retirement note with the evidence sits at the former helper site ("do not re-add
without a link-graph change").

## WP-I — agent harness deferred contracts (R4 REVIEW_LOG re-verification)

- #10 `malformedToolCall` no production consumer — still true; CLOSED by wiring a
  non-fatal consumer in `agent_copilot_dock_widget` (orange notice, run continues).
- #15 non-numeric class domain — consumer side already fail-closed (R4); declaration
  side still accepted anything; CLOSED at the read seam: `readAgentPlan` refuses
  `verification.expectations` shapes that can never verify (non-object, non-array or
  non-numeric `class_values`). Plans that would have failed opaquely at verification
  time now fail typed at read time; no previously-passing plan changes behavior.
- #17 `lowerIrToAgentPlan` step cap — re-verified: all ingestion paths are bounded
  (direct IR: `kMaxNodes=64` at `readWorkflowIr`; recipe path: `kMaxPlanSteps=4096` at
  `readAgentPlan`). The R4 note's mechanism was imprecise (the recipe path is bounded
  by the plan cap, not the IR cap) but the invariant holds; no code change, no new
  restriction invented.

## Independent review outcomes

- P0 `gen_model_library.py --check` exit code was discarded → `sys.exit(main())`; the
  parity gate now actually fails on drift (verified by mutation in /tmp).
- P0/P1 `tests/surface_diff_snapshot.json` regenerated in this change set — captures
  both this round's `rs:infer` description change and the pre-existing
  `artifact_read` workspace-rule prose drift (master-red, now resolved here).
- P1 debug scaffold removed from the domain STAC client; stale repair-engine header
  invariant reworded; `e.g. "gdal:reproject"` doc-comment fixed.
- P2 `cliExitCodeForCode` removed (no production consumer — added by the round, cut by
  review; the two legs that replaced real duplicate tables stay).

## WP-J — drift gates

Existing (not duplicated): capability→registry resolution + intent coverage
(`test_capability_drift`), guidance id/param resolution
(`test_explain_guidance_coverage`), LabSpec corpus (`test_labspec`), CLI surface +
committed-help zero-diff + D8 sidecar gates (#1412), eval negative corpus (by design).

New this round:
1. `test_source_truth_gates` — operator macro/explicit/runtime parity per family;
   recipe corpus operator ids; living-docs (docs/processing|models|agent) operator
   tokens. Exemption lists are tiny and every entry carries its reason.
2. `test_agentbench_corpus` — registry-membership gate for the bench vocabulary.
3. `test_explain_guidance_coverage` — canonical-keyset completeness pin.
4. `test_model_library_generator_parity` — generator/disk byte parity via `--check`.
