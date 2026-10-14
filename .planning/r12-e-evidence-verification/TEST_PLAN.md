# R12-E Test Plan

Environment: Linux host, Qt6 offscreen, `ctest -j1`, heavy builds `flock ~/.cache/exp-rs-heavy-build.lock` + `--parallel 2` max (`scripts/build.sh` cap). Windows/GPU lanes: NOT_RUN (no such hosts available).

## Affected targets (19) — all built from a clean worktree configure and run

| Target | What changed / covered | Run |
|---|---|---|
| test_faultlab | D1 hostile scenario-file cases + full regression | ctest + exe |
| test_faultlab_params_r8 | D2 mistyped-param refusals + parity oracle | ctest + exe |
| test_verify_adapters | D3 nesting-bomb typed refusals on 4 surfaces | ctest + exe |
| test_scientific_state_core | D4 out-of-range int typed refusal + int boundary | ctest + exe |
| test_suitability_adversarial | D5/D6 wrong-type/negative refusals + guards | ctest (30/30) |
| test_verifier_adversarial_14 | D10 coverage-gap roll-up (G-14), D11 truthful budget (G-15) | ctest (19/19) |
| test_explain_validator / test_explain_builder | D13 empty-status gate, D14 trust-note attribution | ctest (24/24) |
| test_preflight_provider | D7 forward-ref deep/cyclic extends chains | ctest + exe |
| test_preflight_report_schema | D8 digest==RFC8785 authority, D9 can_proceed parity | ctest + exe |
| test_preflight_golden | D8 regression (replay equality, no pinned bytes existed) | exe |
| test_preflight / _engine / _render | F5 regression | exe |
| test_grader_engine | D12 cycle refusals; leap-second/lowercase-t pins; 10× digest determinism | ctest (17/17) |
| test_grader_schema | D16 streaming pin + regression | ctest + exe |
| test_grader_adapters | E12 leg-2 NDVI evidence benchmark + regression | ctest + exe |
| test_verify_chain_locale_matrix | D16 four-fork parity gate (14-entry corpus, KATs, locale sweep) | ctest + exe |
| test_fault_matrix | E12 leg-1 scenario benchmark (via sicnu_faultlab link) | ctest + exe |

## RED-first discipline
Every defect fix D1–D14 carries a committed test that failed pre-fix (RED excerpts in each agent report, consolidated in ORACLE_LEDGER.md) and passes post-fix (GREEN×2: two consecutive scoped ctest runs). Guards/pins (leap-second, lowercase-t, 10× digest, parity gate, streaming pin, E12 benchmark) pass on day one by design; the parity gate's teeth were demonstrated by a temporary KAT-literal perturbation (failed as expected, reverted).

## Negative/adversarial matrix (E13)
Covered by the new cases: nesting bombs (faultlab file loader, all adapter surfaces), trailing garbage, embedded NUL, oversized files; mistyped/coerced optional params; wrong-typed/negative persisted numerics; forward-referenced and cyclic config chains; cyclic rubric graphs; contradictory evidence pairs; empty execution status; locale sweep (comma-decimal) over digest equality; 10× determinism loops (grader, benchmark leg-2).
