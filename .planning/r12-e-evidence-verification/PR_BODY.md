# hardening(r12-e): verification, grading, faultlab & evidence-chain hardening

Direction **R12-E** of the R12 parallel hardening wave: quality, correctness and boundary fixes for the *existing* verify / verification / preflight / grader / faultlab / suitability / explain / verify_adapters / scientific_state modules. No new product surface; fail-closed doctrine enforced; typed-error boundaries made real.

## Baseline & conflict check (re-verified at submit time)

- Base: `origin/master@28abef358217520dfbb802efdfdb11a2bf850465` (re-fetched at PR creation: unchanged, 0 commits delta).
- Open PRs re-checked: **#1483** (R12-A: analysis/core/operators-rs/processing) and **#1484** (R12-C: runtime/chunk, science_context, pi) — **zero source-file overlap** with this branch; GraphQL `reviewThreads` on both: none.
- Shared-file discipline: two recorded single-line link additions in `tests/CMakeLists.txt` (`sicnu_faultlab` onto existing `test_verify_chain_locale_matrix` + `test_fault_matrix` targets) and one module-local `src/preflight/CMakeLists.txt` link edge (`sicnu_preflight → sicnu_data`, verified acyclic) — all recorded in `.planning/r12-e-evidence-verification/OWNERSHIP.md`. Textual races with #1483/#1484's appended targets are at distinct anchors.
- Issue leads re-adjudicated against current code (not taken at face value): #1447/#1450/#1464/#1465/#1466 — **10 claimed items found ALREADY_FIXED** with commit evidence (9bdd1bcfcf, 7430953dcc, 9052cb5d82, 08de379dae, d91a669e52) and left untouched; the live tails became the defects below. Full matrix: `.planning/r12-e-evidence-verification/PR_ISSUE_REVIEW_MATRIX.md`.

## Root causes & fixes (each: RED-first test → fix → GREEN×2)

| Area | Root cause | Fix |
|---|---|---|
| faultlab scenario loader (#1447 tail) | bare `CharReaderBuilder`: >1000-deep nesting threw `Json::Exception` across the module's never-throws contract; unbounded file read; comments/trailing garbage accepted | stackLimit=128 + try/catch → typed `faultlab.scenario_malformed`; 4 MiB cap enforced on bytes actually read (cap+1 sentinel); `allowComments=false`/`failIfExtra=true` |
| faultlab params (#1464 doctrine) | `wrong_scale_offset` optional params silently coerced (`offset`→0.0, `metadata`→false); `epoch_index`/channel-`permutation` `isIntegral→asInt64` threw on UInt64>INT64_MAX | present-but-unusable → typed `faultlab.fault_unsupported_params`; `isInt64()` gates |
| verify_adapters | `parseJsonBounded` set stackLimit=128 but never caught the throw → escaped "Never throws" headers of checkpoint/sidecar/probe surfaces | catch → typed nullopt refusal (grader_json precedent); bomb tests on all four surfaces |
| scientific_state (#1465 class) | `readInt` `isIntegral→asInt` threw on out-of-range ints (e.g. `index: 3000000000`); same class in `model_sidecar` version/classes[].id and `asset_state_diff` | `isInt()`/`isUInt64()` gates → typed `InvalidField`; class exhaustively closed in the module (review-verified grep) |
| suitability | persisted counts read via `toDouble(-1.0)`: string/negative values silently became "unknown"; scene numerics wrong-typed→dropped, fractional/negative/out-of-range→truncated/UB | type+sign+range gates → typed `suitability.facts_invalid` / `scene_invalid`; absent keeps the sentinel |
| preflight capability mirror | forward-referenced `extends` chains bypassed the load-time depth check → merge silently truncated policies → band-role check passed with policy dropped | load-order-independent depth/cycle validation over all loaded entries; unhealthy mirror → typed Unavailable; unresolved parent at the bound stays a benign stop |
| preflight digests (#1450 tail) | report/request/projection serializers used a private jsoncpp writer at precision 12 — unreproducible via the platform canonical authority | all three ride `sicnu::data::canonicalizeJsonRfc8785` (one recorded PUBLIC link edge, acyclic); no persisted pins/non-test consumers exist (grep-verified ×2) |
| preflight render | agent `can_proceed` weaker than teaching `all_clear` | aligned fail-closed (`verdict=="ok" && findings.empty()`) |
| verification roll-up | `buildReport` folded only node-mapped checks: an unmapped Fail left `report.status==Pass` while `counters.fail==1`; missing evidence faked `budgetUsage.exceeded` | uncovered results fold worst-wins + policy-promoted (promotion recorded), gap surfaced as `VERIFY.SPEC_INVALID`; truthful `VERIFY.EVIDENCE_UNAVAILABLE` |
| grader rubric | `orderedAfter` self-references/cycles validated "exists" only → vacuously satisfiable | iterative three-color DFS refusal via existing `SchemaShapeInvalid` |
| explain | empty execution status flowed to a SystemFact badge and produced documents the strict parser itself refuses; skipped-step note asserted authored text as run fact | adapter treats empty state as unavailable; builder refuses `ungrounded_execution_evidence`; validator raises typed `empty_content`; note re-attributed to authored guidance |
| SHA-256 authority (#1450 tail) | four deliberate per-leaf forks byte-equivalent but unpinned | parity gate: 14-entry boundary corpus + NIST KATs + streaming chunk-splits + comma-decimal locale sweep; grader update-after-finalize semantics pinned |

Plus **E12 behavior benchmark**: clean teaching scenario passes with byte-stable digests; leakage/cloud-mask/scale-offset variants localize to the exact fault family and observables; grader leg flips exactly one criterion per injected fault with the precise reason slug; seed fixtures provably unmutated.

## Tests & resources (local, real)

- 19 affected test executables rebuilt and run fully: **all pass** (e.g. test_faultlab 539 assertions/41 cases, test_suitability_adversarial 440/32, test_grader_engine 320/17, test_verify_chain_locale_matrix 748/3, test_fault_matrix 221/16; full list in `.planning/.../TEST_PLAN.md`).
- Every defect fix carries a committed RED-first failing test; GREEN×2 (two consecutive scoped runs) per fix; guards/pins explicitly labeled.
- Resource discipline: fresh worktree configure; all builds under a cross-process `flock` with `--parallel 2` cap; ctest `-j1`; `QT_QPA_PLATFORM=offscreen`; no CI awaited per directive. Windows/GPU lanes: **NOT_RUN** (no hosts) — new code is portable-construct-only per reviewer scan.
- `git diff --check` clean.

## Independent review (non-implementers, two rounds)

- **Round 1** (REVIEWER-A correctness / REVIEWER-B integration): 0 P0, 1 P1, 13 P2 — all addressed (`717f5537f4`, `e067a523b2`, docs commit). Verified-sound list includes acyclicity of the new link edge, vocabulary-seal compliance (zero invented error codes), and ownership conformance.
- **Round 2** (REVIEWER-C / REVIEWER-D, fresh): one residual P1 of the same class (faultlab int64 gates) → fixed (`e067a523b2`) → closure re-verified by Reviewer-C: **0 P0 / 0 P1**, no new P2.
- Reviewer counterexamples were re-derived and re-run by the main agent's fix loop; details in `REVIEW_R1.md` / `REVIEW_R2.md`.

## Remaining risks / follow-ups (recorded, none blocking)

- verify_adapters have no path-containment/remote-timeout policy (deliberately dumb adapters; needs an owner-level policy ruling before implementation).
- verify↔verification verdict-class divergence on identical facts (deliberate dual lattices); owner ruling needed to harmonize.
- `ExplanationValidator` guidance semantic cross-check documented as structural-only (follow-up).
- P3 observations recorded in `.planning/r12-e-evidence-verification/DECISIONS.md`.

Planning/evidence set: `.planning/r12-e-evidence-verification/` (BASELINE, PR_ISSUE_REVIEW_MATRIX, OWNERSHIP, WORK_PACKAGES, REPRODUCERS, TEST_PLAN, ORACLE_LEDGER, PERFORMANCE, DECISIONS, REVIEW_R1, REVIEW_R2, FINAL_REPORT).
