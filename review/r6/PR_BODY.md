# refactor(registry): converge runtime registries, manifests and surface contracts

R6 source-of-truth convergence round — refs #1393, #1394. No new capability; every
change renames stale ids to the live registry's truth, completes authored data, or pins
a machine-readable gate so the drift cannot silently return. Full decision log with
per-item evidence: `review/r6/DECISIONS.md` (+ the five census files next to it).

## WP-A — operator registration single truth

Census @ 1e28de867: 189 registered ids / 5 families; rs/gdal/opencv/otb macro-vs-explicit
aligned (the #1398 exposure class stays closed); **io was 13 macros / 17 explicit**.
Added the 4 missing io macros (fabric tools) → every family is exact-parity.
**Gate:** `test_source_truth_gates` parses the five init TUs and pins
macro set == explicit set == live `operatorNames()` per family (MSVC dead-strip safety,
#707 rationale).

## WP-B — vocabulary (evidence-backed, no blind renames)

- Bench corpus six (see WP-C): `rs:change_detect`→`rs:change_detection`,
  `rs:co_register`→`rs:align`, `rs:train_classifier`→`rs:supervised_classification`
  (method `rf` exists in the backend factory), `rs:pan_sharpen`→`gdal:pansharpen`
  (decoy must be REAL), `temporal:smooth`/`temporal:gap_fill`→`rs:temporal_smooth`/
  `rs:temporal_gap_fill` (the `temporal:` prefix owns the 12 collection tools).
- `workflow_orchestrator_tool.cpp`: demo chains now only name registered ids —
  sensor-conditional imports (same partition as `sensorProfile()`), `rs:change_cva`,
  `rs:threshold_raster`, `rs:fusion_gram_schmidt`, `opencv:gaussian_blur`
  (real params), `rs:supervised_classification{method:rf}`.
- `workflow_repair_engine`: CRS repair → `gdal:reproject{dstCrs}` (real param name);
  resolution repair → `rs:resample{resolution}` (square cells only); **DataTypeMismatch
  auto-repair retired** — no registered operator performs dtype conversion, so the old
  `rs:convert_dtype` rule emitted workflows that could never execute (violation still
  reported, mirroring the DimensionMismatch precedent).
- `workflow_cost_estimator` coefficient table re-keyed to registered ids (closed table
  was keyed on ids that never existed; unknown ops silently default to 1.0).
- `otb_svm_classification` workflowHint now points at `rs:supervised_classification`
  predict-only; two `rs:inference` prose sites → `rs:infer`; preset catalog
  (`gdal:import`/`gdal:slope`/`gdal:hillshade`) → `rs:landsat_import` / `io:translate` /
  `rs:terrain_analysis{product}`. `cartography:*` preset ids are legitimate tool-surface
  vocabulary — NOT drift, untouched.
- Living docs: `gdal:warp`→`io:warp` (grid policy); foundation-5 "future work" bullet
  rewritten (the four ops landed). Historical records (`docs/adr`, `docs/superpowers`)
  and the fixture-mirrored `docs/experiments/debugging.md` left alone by design.

## WP-C — bench corpus (generator → regenerate → bump → re-pin)

SPEC edited, pack regenerated (deterministic; triple-run hash-verified),
`suite.json.version` 1.0.0 → **1.1.0**, digest pins updated in the same commit — no
hand-edited digests. Counts/failure classes are id-independent and re-verified by the
end-to-end grading test. **Gate:** every allowed/redundant/fault/trace tool id must
resolve in the live registry; `harness:*`/`model:`/`map:` (documented opaque families)
and the rogue probe `rs:forbidden_augment` are exempt — the rogue is pinned to its
registry ABSENCE. Vocabulary contract documented in `data/agent/bench/README.md`.

## WP-D — guidance sidecars

13 missing (file, key) pairs authored across the 6 lean files; canonical keyset derived
from the loader's closed 12-key allowlist (not majority vote); `role` stays absent
everywhere (generic-corpus convention). **Gate:** `test_explain_guidance_coverage` now
pins the full optional keyset per file.

## WP-E — model manifests (spec was the stale side)

**4 manifests were disk-newer, not 2** — `ssl-embedding-encoder` and
`unet-buildings-s2` also carried `id`/`model_version`/`license`(/`source`) identity keys
the generator never emitted; a blind regenerate would have regressed all four.
`gen_model_library.py` extended (`input_size`, `resize`, `detection`,
`extra_identity`, swin 4-band normalization); regenerated → all 24 generator manifests
byte-identical to disk (regen is a no-op; parity check exits 0). The two v1-era
disk-only templates `sam-building`/`yolo-buildings` (zero code/test references;
superseded by `sam-buildings-hr`/`yolo-building-detection`) are **deleted**;
doc-comment + README updated, v1 parsing section stays for user manifests.
**Gate:** ctest `test_model_library_generator_parity` runs `gen_model_library.py --check`.

## WP-F — STAC dual client converged

`src/geospatial/stac/stac_client.*` is now the single data/transport authority (request
building, egress/SSRF policy, pagination continuations, timeouts, detached fetch);
`src/app/stac_client.*` is a thin Qt adapter — public Qt-facing API unchanged (dialog
churns zero), late-arrival landing policy preserved byte-for-byte. No third
abstraction. Accepted delta, inherent to "CPL is the single HTTP implementation"
(ADR 0139): hop-by-hop redirect re-validation is not reproducible; egress policy is
enforced on every URL the domain client dispatches. Test wiring: `test_stac_client`,
`test_m2_batch_b_dialogs` link `Sicnu::Geospatial`.

## WP-G — error contract (internal convergence, external compat proven)

The two alias tables that lived outside the internal model moved INTO
`harness_error.cpp` `kEntries`: `cliExitCodeForSessionRefusal` (was an if-chain in
`cli_agent_ops_commands.cpp`) and `mcpToolCodeForLegacy` (was an inline table in
`mcp_server.cpp`), plus the taxonomy→exit-code projection `cliExitCodeForCode`.
CLI still prints `E-n:SYMBOL` + `CliErrorDetails` (same numbers) and MCP/agent still
emits `{success:false,error:{...}}` (same values) — both pinned by existing + new
`test_harness_error` legs. No version bump: nothing observable moved.

## WP-H — WHOLE_ARCHIVE stopgaps: all four retired with evidence

#1335/#1366 gave `sicnu_agent` (SHARED) a PUBLIC `sicnu_agent_loop` link edge — the
loop's PIC objects are embedded in and exported by `libsicnu_agent.so`. Per-site
experiment (remove → relink → run green, zero undefined refs at link or load):
`sicnu_add_test` helper (222 targets; representative relinks + runs:
test_capability_drift 748, test_source_truth_gates, test_explain_guidance_coverage,
test_verifier_robustness_r4 — the 12 test TUs using agent_loop headers are wired
separately and don't use the helper), `test_tool_call_dispatcher` (226),
`test_output_verifier` (40), `test_llm_streaming_client` (74). Retirement note with
evidence left at the former helper site.

## Independent review (agent 3)

READY-AFTER-FIXES → fixes applied in this same change set:
- P0: `gen_model_library.py --check` now propagates its exit code (`sys.exit(main())`)
  — the parity gate can actually fail.
- P0/P1: `tests/surface_diff_snapshot.json` regenerated in the same change set —
  captures this round's `rs:infer` description change AND the pre-existing
  `artifact_read` workspace-rule drift (a master-red gate, resolved here).
  `test_surface_snapshot`: 17872 assertions green after regen.
- P1: debug scaffold removed from the domain STAC client; stale repair-engine header
  invariant reworded; `e.g. "gdal:reproject"` doc-comment fixed.
- P2: `cliExitCodeForCode` removed (no production consumer); WP-F accepted deltas
  (whole-page Item validation, typed bbox refusal, out-of-order same-generation
  landings) documented in `review/r6/DECISIONS.md`.

## WP-I — agent harness deferred contracts (R4 REVIEW_LOG re-verification)

- `malformedToolCall` production consumer (was backlog #10): wired non-fatal
  observability in `agent_copilot_dock_widget` (run continues).
- Non-numeric class domain (was backlog #15): `readAgentPlan` now refuses
  `verification.expectations` declarations that can never verify (typed `INVALID_PLAN`
  at the read seam; consumer-side fail-closed behavior from R4 unchanged).
- `lowerIrToAgentPlan` node limit (#17): re-verified bounded on all ingestion paths
  (direct IR 64-node cap; recipe path 4096-step plan cap) — invariant holds, no change.

## WP-J — drift gates

New: `test_source_truth_gates` (operator parity / recipe ids / living-docs ids; tiny
reason-annotated exemption lists), bench-corpus vocabulary gate, guidance keyset
completeness pin, model generator parity ctest. Existing gates reused, not duplicated:
`test_capability_drift`, guidance id/param resolution, `test_labspec`, #1412's CLI
surface + help zero-diff + D8 gates. Every gate is bounded (known files / one data
directory), no full-repo sweeps.

## Verification

- Full configure + build at `-j2` (strict), then incremental rebuild of all touched
  targets; ctest for every touched suite (list in test-run notes) — no reliance on
  online CI.
- Corpus regenerate double-run: byte-stable (aggregate hash equal).
- Guidance authored content linted against loader rules (required trio, state
  vocabulary, reference kinds, non-empty entries).
- Touched tests run twice (ordering/per-process isolation per #1414).

🤖 Generated with [ZCode](https://zenm.ai)
