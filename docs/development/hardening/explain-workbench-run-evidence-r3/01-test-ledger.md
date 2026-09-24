# R3 Track 07 — Explain / Workbench Panel / Run-Scoped Evidence — Test Ledger

Branch: `hardening/r3-explain-workbench-run-evidence-r3` (base: origin/master `5697ca2ad`).
Scope: close the #1282 tail items — a why-this-step UI surface reusing the deterministic
view model, real run-scoped evidence through the existing adapter, and the full UI
lifecycle. No new product direction; no explanation semantics outside `sicnu_explain`.

## What changed

| Area | Files |
|---|---|
| Panel (render-only, provider injection, generation counter) | `src/app/workbench/step_explanation_panel.{h,cpp}` (new) |
| Inspector section (node selection → request projection; run-scoped evidence lifecycle) | `src/app/workbench/step_explanation_section.{h,cpp}` (new) |
| Selection seam (IR 2.0 canvas node id rides the unified snapshot, mission-task pattern) | `src/app/workbench/selection_context.{h,cpp}` |
| `explain:step` run scope (`runId` + `provenanceDirectory`, pair-refused; plan-only response shape unchanged) + shared guidance store accessor | `src/agent/spatial_tools/explain_step_tool.{h,cpp}` |
| Shell wiring (2 providers + document provider registration; dock signal connections) | `src/app/main_window_workbench.cpp`, `src/app/main_window_view.cpp` |
| Build wiring | `src/app/CMakeLists.txt`, `tests/CMakeLists.txt` |
| Oracles | `tests/test_explain_step_panel.cpp` (new, 15 cases), `tests/test_explain_agent_tool.cpp` (+6 cases) |

## Oracle results (final tree, each key suite run twice consecutively)

| Suite | Result |
|---|---|
| `test_explain_step_panel` | **ALL PASSED 147/147, 15/15 — ×2** |
| `test_explain_agent_tool` | **ALL PASSED 73/73, 12/12 — ×2** (includes 6 new run-scoped cases) |
| `test_selection_context` | 60 assertions / 13 cases (adjacent — snapshot field added) |
| `test_inspector_host` | 27 assertions / 6 cases (adjacent) |
| `test_provenance_section` | 61 assertions / 6 cases (adjacent) |
| `test_build_wiring_drift` | 34 assertions / 7 cases (CMake changed) |
| `sicnu_geo_rs` | named product target built (see PR body for final state) |

## Sabotage evidence (each run on the exact tree, then reverted and re-verified green)

| Sabotage | Red |
|---|---|
| S1: tool reverted to master shape (no `runId`/`provenanceDirectory` handling) | `test_explain_agent_tool` 4 cases / 8 assertions FAIL |
| S2: panel honest-unknown branch removed (`if (false && !executionShown)`) | `test_explain_step_panel` 6 cases / 7 assertions FAIL |
| S3: section keeps the previous run's adapter when a re-attach fails to parse | tampered-re-attach case FAIL (stale 状态: Succeeded served after the record corrupted) |

## Honesty contract pinned by the oracles

- Plan-only / success run / failed run / unknown run / tampered record produce
  **predictably different** renders; absent evidence renders `执行情况未知（…原因）`,
  never a blank, never a synthesized status, never fabricated timestamps
  (the provenance record has no wall-clock stamps and none appear).
- Badges come from `FactProvenance` via the view model: authored text claiming to be
  `系统事实` still renders as `编写指引` (impersonation test); `系统事实` appears only on
  execution lines (the registry adapter intentionally leaves operator machine-purpose
  empty).
- Guidance removal removes authored lines but keeps the port-fact state synthesis and
  adds the honest no-guidance trust note; an authored state contradiction renders only
  as typed problem + trust note — port facts (`DN → Radiance`) cannot be overwritten.
- Run A → run B (separate provenance directories) leaves no artifact identity of A;
  re-attaching a corrupted record for the SAME run drops the stale status immediately;
  `clearRunEvidence()` (run start / session boundary) drops evidence at once.
- Inspector host lifecycle: unsupported selection → placeholder, zero generations;
  identical snapshots re-populate exactly once with byte-identical render (no
  accumulation); hide/show of the section triggers no population; selection leaving and
  returning re-renders cleanly.
- `explain:step` plan-only responses keep their exact historical shape (no
  `evidenceProblems` member, no execution facts); a lone `runId` or lone
  `provenanceDirectory` is refused `INVALID_PARAMETER` instead of partially interpreted;
  load refusals surface verbatim in `evidenceProblems`.

## Known limits

- The panel is synchronous by design: every source is a bounded in-memory lookup and
  the (expensive) provenance-directory load happens once in the shell's run-finish
  handler, not per render. `InspectorSection::cancelPending()` stays the default no-op —
  the same recorded trade-off as `ProvenanceSection`.
- Engine 2.0 workflow runs and AgentOps sessions do not produce `d17_provenance`
  records, so for those run refs the panel honestly shows 执行情况未知; the agent tool
  accepts an explicit `provenanceDirectory` for any run that has a record.
- The section explains the node selected on the D17 canvas; run-only selections
  (Processing History) do not identify a step, so the section does not claim support
  for them.
