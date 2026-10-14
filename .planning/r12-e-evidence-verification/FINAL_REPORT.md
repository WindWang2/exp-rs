# R12-E Final Engineering Report — 科学验证、评分、故障实验与证据事实链

Branch: `hardening/r12-e-verification-evidence` · Worktree: `exp-rs-r12-e-verification` · Base: `origin/master@28abef358217520dfbb802efdfdb11a2bf850465` (unchanged at PR time — re-fetched).

## 1. Acceptance matrix (directive §8)

| 证据阶段 | 结果 | 证据 |
|---|---|---|
| 基线 | PASS | BASELINE.md: origin/master SHA, open PRs #1483/#1484 (file lists, zero review threads via GraphQL), 20 open issues, full local worktree enumeration; master unchanged at PR time (0 commits delta) |
| 所有权 | PASS | OWNERSHIP.md: zero source overlap with #1483/#1484; two recorded single-line tests/CMakeLists.txt exceptions + one src/preflight/CMakeLists.txt edge (acyclicity independently verified by Reviewer-B); no other-direction file touched |
| 现有模块盘点 | PASS | 7-track audit (T1–T7): entry maps, producer/consumer chains, error-code domains; issue leads #1447/#1450/#1464/#1465/#1466 re-adjudicated — 10 items ALREADY_FIXED with commit evidence (9bdd1bcfcf, 7430953dcc, 9052cb5d82, 08de379dae, d91a669e52), no rework |
| Oracle | PASS | ORACLE_LEDGER.md D1–D17: every fix RED-first (failing test committed before fix) → GREEN×2 (two scoped ctest runs); REPRODUCERS.md lists the minimal failing input + command per defect |
| 正确性 | PASS | No silent success added; negative cases committed (nesting bombs, mistyped params, wrong-typed numerics, forward-ref/cyclic chains, cyclic rubrics, empty status, contradictory evidence); E12 teaching benchmark pins baseline-pass + per-variant precise localization + seed isolation |
| 性能 | PASS (measured) | PERFORMANCE.md: no hot-path change; parity gate ~1-2 s; benchmark 0.75 s; test RSS ≤7 MB observed; build times recorded. No fabricated baselines |
| 跨平台 | NOT_RUN | Linux-only host available. New code uses portable constructs only (reviewer-D scan: no POSIX-only calls); Windows lane NOT_RUN honestly; locale sweep incl. comma-decimal runs in the parity gate |
| 资源 | PASS | All heavy builds under `flock ~/.cache/exp-rs-heavy-build.lock`, `--parallel 2` cap (scripts/build.sh discipline), ctest -j1; memory checks before builds (MemAvailable ≥40/65 GB observed); no other worktree's cache touched |
| 独立 review | PASS | Round 1: REVIEWER-A 0P0/0P1/7P2 + REVIEWER-B 0P0/1P1/6P2 (both non-implementers) → all fixed (717f5537f4, e067a523b2 + docs) → Round 2: REVIEWER-C/D fresh; residual P1 closed (e067a523b2) and closure re-verified by Reviewer-C: **0 P0/P1**; TEST_PLAN/REVIEW_R1/REVIEW_R2 committed |
| 交付 | PASS | `git diff --check` clean; 13 thematic commits; planning set complete; PR submitted (URL below) |

## 2. Defects fixed (12 substantive + gates/benchmark)

| ID | Root cause (one line) | Commit |
|---|---|---|
| D1 | faultlab scenario file loader: bare CharReaderBuilder — nesting bomb threw across never-throws API; unbounded read; comments/extra accepted | c21f097a68, e067a523b2 |
| D2 | wrong_scale_offset silently coerced mistyped optional params | c21f097a68 |
| D3 | verify_adapters parseJsonBounded let jsoncpp stackLimit exception escape "Never throws" contracts | d89a313bf0 |
| D4 | scientific_state readInt isIntegral→asInt threw on out-of-range (class closed module-wide: model_sidecar, asset_state_diff, faultlab max_bytes, transforms int64) | 0344543bce, 717f5537f4, e067a523b2 |
| D5/D6 | suitability facts/scene wrong-typed/negative/fractional numerics silently defaulted/dropped/truncated | 477ddd6683, 717f5537f4, e067a523b2 |
| D7 | capability mirror depth refusal bypassable by forward-referenced extends chains → silent policy truncation → pass | e15ff4f79a, 717f5537f4 |
| D8 | preflight digests on private precision-12 writer, unreproducible via the platform RFC 8785 authority | e15ff4f79a, 717f5537f4 |
| D9 | agent can_proceed weaker than teaching all_clear | e15ff4f79a |
| D10 | verification roll-up dropped node-unmapped Fail results → report-level false Pass | 6709ff7d1d, 717f5537f4 |
| D11 | missing evidence mislabeled as budget overrun | 6709ff7d1d |
| D12 | grader accepted self/cyclic orderedAfter rubrics | c05a350dba |
| D13/D14/D15 | explain: empty status → SystemFact badge + self-refusing document; skip note overclaim; validator doc drift | 1a771c4b7a |
| D16 | four SHA-256 forks unpinned → parity gate (14-entry corpus, KATs, locale sweep) + grader streaming pin | 095614d9f7 |
| D17 | E12 teaching behavior benchmark (faultlab leg + grader leg) | 959d19aae7 |

## 3. Already-fixed confirmations (no rework, evidence kept)
#1464 grader timestamps (parseSequencingStamp + calendar validation), #1464 cell_size (consumed by observables), #1465 std::equal OOB ×4 (length gates), #1466 suitability integer fail-open (boundedIntegral), #1450 suitability digests (RFC 8785), #1450 faultlab locale pin (ClassicNumericLocale), #1447 faultlab transform type gates + capability_mirror guard. See PR_ISSUE_REVIEW_MATRIX.md.

## 4. Test totals (final sweep, this branch)
19 test executables: **all pass** (assertion totals per binary in TEST_PLAN.md / sweep log; e.g. test_faultlab 539/41, test_suitability_adversarial 440/32, test_grader_engine 320/17, test_verify_chain_locale_matrix 748/3, test_fault_matrix 221/16). New RED-first cases: 30+ across the touched suites.

## 5. Remaining risks / follow-ups (none blocking)
- verify_adapters path-containment/remote-timeout policy: recorded, needs an owner-level policy ruling before implementation (DECISIONS.md).
- verify↔verification verdict-class divergence for identical facts: deliberate dual lattices; needs owner ruling.
- ExplanationValidator guidance semantic cross-check: documented as structural-only; follow-up.
- tests/CMakeLists.txt will textually race with open PRs #1483/#1484 (both append targets); our two one-line link additions are at distinct anchors — trivial to rebase either way.
- Windows/GPU lanes NOT_RUN (no hosts).
- P3 observations from Reviewer-C (canonicalFindingText tie-break precision; absent-evidence silence in explain adapter; canonicalizer ≥2^53/NaN narrowing pre-existing in data layer) — recorded, not in R12-E scope to change.

## 6. PR
**Created and verified**: https://github.com/WindWang2/exp-rs/pull/1497 — `gh pr view` confirms number=1497, state=OPEN, head=hardening/r12-e-verification-evidence, base=master, draft=false, 14 commits. Non-draft, no auto-merge, CI not awaited per directive. Branch and worktree retained for maintainer review.
