# ISSUE_TRIAGE — R28-T05

Legend: CONFIRMED_OPEN / FIXED_OPEN (fixed in tree, issue still open — verify + close via PR) / PARTIALLY_FIXED / NOT_REPRODUCED / SUPERSEDED / OUT_OF_SCOPE / TO_VERIFY (source-level check pending, resolved below as work proceeds).

Baseline for all checks: `50cc3255c5` (this worktree's HEAD).

## #1804 [P3][workflow-experiment] composite (8 sub-items)

| Sub | What | Status | Evidence |
|---|---|---|---|
| WE-18 | `${task.12abc}` parsed as task 12 | **CONFIRMED_OPEN** | `src/workflow/placeholder_grammar.cpp:52` — `std::stol("12abc")` returns 12 without throwing; no whole-string-consumed check; no test |
| WE-19 | grader conflict double `!=`; pointer to moved vector | **CONFIRMED_OPEN (first half)** | `src/grader/grader_matcher.cpp:540` `item->value != reference` raw compare, no tolerance. Moved-vector half: TO_VERIFY during fix |
| WE-24 | MAPE=0 when all truths zero | **PARTIALLY_FIXED** | `evaluation.cpp:392,399` counts `mapeSkippedZeros` and emits `mape_skipped_zeros`, but the `mape` scalar still reads `0.0` — WP06 requires explicit undefined presentation |
| WE-25 | 8192-label confusion → 512 MB | **PARTIALLY_FIXED** | `evaluation.cpp:265` cap `kMaxConfusionLabels=8192` refuses beyond, but at cap the dense 8192² double matrix is still ~512 MB; WP06 wants sparse/budgeted representation |
| WE-26 | planning_context accepts negative/non-finite resolution | **CONFIRMED_OPEN** | `src/planner/planning_context.cpp:58-66` — only `isNumeric()` checked; negative/NaN/Inf accepted into `asset.resolutionM` (write side guards `>= 0` but parse side doesn't reject). NOTE: `src/planner/` is T03-owned; surgical parse-side reject to be done with a DECISIONS entry + handoff note |
| WE-29 | bundle import readAll/hash unbounded | **CONFIRMED_OPEN** | `reproduction_bundle_import.cpp:828` relocation loop `source.readAll()` no byte cap |
| WE-30 | relocation overwrites existing files | **CONFIRMED_OPEN** | `reproduction_bundle_import.cpp:843` `QFile::remove(destination)` with `// relocation is last-writer-wins` — no conflict report |
| WE-34 | run_bridge `diagnostics().first()` on empty | **CONFIRMED_OPEN** | `run_bridge.cpp:566,577,589,615` — 4 unguarded `.first()` calls in failure branches |

## WE P1/P2 singles (fixed by #1809/#1812 unless noted)

Issues with in-source fix references (verified present, behavior confirmation via baseline test run of the corresponding suites): #1744 (WE-11, 4 refs), #1768, #1770 (2), #1771, #1773 (2), #1775 (2), #1776 (6), #1777 (4), #1780 (2), #1781 (2), #1782, #1783 (4), #1784, #1785, #1786, #1787 (3). Preliminary class: **FIXED_OPEN** (pending test evidence; downgrade any that fail in baseline run).

No source reference found — direct checks done:
- #1769 (WE-06 load bounds): **FIXED_OPEN** — session_journal.cpp now has kMaxDocumentBytes caps + "never an unbounded rdbuf slurp" (fixed by #1812); test confirmation in baseline run.
- #1772 (WE-09 grader text vs verdict): **CONFIRMED_OPEN** — `replay_deviation.cpp:235` puts `environment` in kIdentityDimensions unconditionally (env drift forces Deviated) while `:65-68` remediation text promises "reported, not punished; align only when Strict determinism". Strict-grade handling exists for fingerprints (:294) but not for the env dimension.
- #1774 (WE-13 size_t underflow): **FIXED_OPEN** — repair_planner.cpp:297-301 guarded ternary, comment refs #1774.
- #1778 (WE-17 rejection ordering): **FIXED_OPEN** — repair_planner.cpp:374-391 refusal-LAST stable_sort, comment refs #1778.
- #1779 (WE-20 multi-parent first-bind): **FIXED_OPEN** — task_center.cpp:1561/1813 ambiguous keyword refs stay raw + launch gate refuses (comment refs #1779). Note task_center is T02-owned; fix already on master.

## build-ci-tests family (WP01/WP10/WP11 overlap)

| Issue | Status | Evidence |
|---|---|---|
| #1732 BCT-01 master no branch protection | **OUT_OF_SCOPE** | task contract forbids ruleset/branch-protection changes |
| #1734 BCT-03 $LASTEXITCODE residue | TO_VERIFY (Windows CI script; static check) | |
| #1735 BCT-04 INFO << QJsonArray compile fail | TO_VERIFY via real build | |
| #1736 BCT-05 vacuous CHECK | **FIXED_OPEN** | `tests/test_adversarial_m6.cpp:218` comment documents replacement |
| #1737 BCT-16 masked contract drift | TO_VERIFY via baseline run | |
| #1748 BCT-11 PENDING hashes | **FIXED_OPEN** | `packaging/appimage-tool-checksums.txt` carries two real SHA256; fail-closed retained |
| #1749 BCT-13 bundle missing libsicnu_*.so | **PARTIALLY_FIXED** | `scripts/build_offline_bundle.sh:145-177` stages libs with warnings; real bundle run in WP10 validates |
| #1750 BCT-20 SUCCEED-as-skip | TO_VERIFY | |
| #1751 BCT-22 sleeps (505 repo-wide) | **PARTIALLY_FIXED** | 71 remain in tests/*.cpp; track-owned tests have 7 (WP11 scope: mine + latch conversion where structural) |

## Delivery/infra (WP10)

| Issue | Status | Evidence |
|---|---|---|
| #1697 offline bundle --version traversal | **FIXED_OPEN** | `build_offline_bundle.sh:41-46` whitelist comment `#1697`; WP10 re-verifies |
| #1698 INSTALL_RPATH + sicnu_worker install | **FIXED_OPEN** | `CMakeLists.txt:259-269` RPATH; `src/cli/CMakeLists.txt:268` install(TARGETS sicnu_worker); WP10 real `cmake --install` validates |
| #1623 zh_CN.qm no install rule | **FIXED_OPEN** | `CMakeLists.txt:978-1011` lrelease + install beside executable |
| #1682/#1688 Windows cmd injection | NOT_EXECUTED (no Windows host) | static note only |
| #1682 WE-adjacent Windows/macOS CI zero test coverage | NOT_EXECUTED | |

## Pre-#1800 tech-debt series in track modules (spot-check 2026-11-05)

| Issue | Status | Note |
|---|---|---|
| #1594 workflow substitute re-scan | FIXED_OPEN | single-pass rewrite refs #1594 |
| #1580 bundle run_id unvalidated | FIXED_OPEN | RunId::fromString boundary in importer |
| #1581 bundle lineage slice | FIXED_OPEN | R24-B direction-preserving slices |
| #1582 eval unpaired rows vanish | FIXED_OPEN | coverage accounting refs #1582 |
| #1593 lab_pack stackLimit | FIXED_OPEN | pinned stackLimit 128 |
| #1597 cli parseOptions skip | FIXED_OPEN | typed refusals refs #1597 |
| #1607 teaching jsoncpp stackLimit | FIXED_OPEN | pinned + typed catch refs #1607 |
| #1610 faultlab checkNumber vacuous delta | FIXED_OPEN | R18-E V3 baseline discipline |
| #1602 verify_adapters readFileBounded 64MB | FIXED_OPEN | buffer sized from file stat |
| #1612 faultlab kappa filtered/unfiltered | FIXED_OPEN (pending suite) | fix comment at fault_observables.cpp:279 |
| #1611 faultlab index_pair | TO_VERIFY (suite) | fixture-side |
| #1613 faultlab verifier rules_ref | FIXED_OPEN (ref found) | fault_runner.cpp |
| #1600 verify grid bounded 3x3 | FIXED_OPEN (refs found) | verify_engine.cpp |
| #1601 two verifier cores duplication | DEFERRED (architectural) | pre-existing platform shape; not this round's closure path; handoff note |
| #1606 contracts output params dropped | TO_VERIFY | fromCanonicalJson path |
| #1634/#1652/#1653/#1654/#1655 composites | NOT_TRIAGED (P3 tails) | outside this round's evidence chains |

## New defects found by this round's own gates

| Defect | Status | Evidence |
|---|---|---|
| No-OpenCV configure is dishonest: configure exits 0 with "SIFT disabled", build dies ~1100 files deep at sicnu_processing/ir_mad_kernels.cpp (opencv2/core.hpp missing). Root CMakeLists' "OpenCV optional" claim stale since 7427e6a056. | **CONFIRMED_OPEN → FIXED (306b4071e4)** | redbase first build: EXIT=0 configure then make error 2; fixed build: EXIT=1 configure with remedy message; with-OpenCV reconfigure EXIT=0 |

## NEW defect found by this round's own delivery chain (WP12 chain ①)

**D-T05-CHAIN1 (ROOT-CAUSED by R2, owner = T02 execution lane)**: the chunked
operator resume marker makes every repeated CLI pipeline run a false green.

Reproduction (deterministic, R2-verified): fresh dir, absolute-path pipeline
over `rs:spectral_index` → first run writes the artifact; **every** subsequent
run of the same operator (any directory, any workspace) prints
`[warning] Skipping asset registration; output not openable: <out>` +
`Pipeline succeeded (1 steps)`, exit 0, **no file**. Decisive proof: deleting
`/tmp/sicnu_operators/chunked-*.published|.lock` makes the next run produce the
artifact again.

Mechanism (call chain):
1. `src/cli/rs_pipeline_runner.cpp:610` `runFromJsonChecked` → `WorkflowRunCoordinator::startTrackedPipeline`.
2. `src/workflow/workflow_run_coordinator.cpp:738` → `TaskCenter::submitPipeline`.
3. Non-fused steps get **no** `jobExecutor` (`task_center.cpp:2897-2918`; isolated route is `Off` by default — `worker_execution_route.cpp:43`), so `JobEngine` builds a default `RSOperatorContext` with an **empty workDir** (`job_engine.cpp:1088`).
4. `rs_operator_context.cpp:16-20` `ensureWorkDir("")` → `temp_directory_path()/"sicnu_operators"`.
5. `chunked_run.cpp:104-114` derives `stateBase = /tmp/sicnu_operators/chunked-<op>-<input>-<partition>` because `options.resumeStateBase` is empty.
6. `resumable_tile_run.cpp:299-305` — when that `.published` marker's identity key matches, `execute()` returns `alreadyPublished=true` with **zero** compute/consume/publish.
7. `rs_spectral_index_operator.cpp:1122` puts `already_published:true, tiles_computed:0` in the payload → step Completed → pipeline Completed → CLI success.

The marker key (`tileRunIdentityKey`) covers operator/input/partition identity
only — **not** the output path, workspace, or process — so it is a global
cross-workspace claim in shared `/tmp` and nothing re-verifies the artifact
exists. Same shape at `rs_change_streaming.cpp:696/738`; the failing fast-lane
test `Deleted output self-heals to a cache miss (#667)` is the same family.

Handoff: fix belongs in the T02 lane (marker key must include the output
identity, or the published claim must be re-verified against the declared
output before reporting Completed). This track's chain scripts clear the
regenerable scratch before their runs (documented inline) so the chains
exercise the real write path.
