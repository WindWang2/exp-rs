## Problem statement and baseline

The 7.0 merge wave was followed by three post-merge build repairs (#833
missing std includes, #834 GDAL 3.8↔3.13 VSI API drift, #835 a `_WIN32`
namespace break breaking the POSIX build) — evidence that vertical-track
verification was stronger than whole-platform integration verification.
Baseline audit (`BASELINE.md`): master `322dfd3876`, no open PRs/issues,
all surviving branches map to merged PRs; the 7.0 verification layer (PR
#831) is the direct predecessor and is extended, not rebuilt.

## Architecture and ownership

- New verification capabilities extend the 7.0 observability core
  (`src/runtime/observability/**`) at EXISTING broadcast seams — no second
  event bus, scheduler, model runtime, or publication path
  (`OWNERSHIP.md`, `ARCHITECTURE.md`).
- First-party GDAL version logic converges on one new seam
  (`src/geospatial/util/gdal_compat.h`); vendored QGIS untouched.

## Exact behavior changes

1. Additive trace adapters at five seams (WorkflowRunCoordinator,
   TaskCenter, OutputCommitter, DatasetStore, ExperimentStore), completing
   the documented chain; disabled path unchanged (one relaxed atomic load).
2. Three new `SICNU_FAULT_POINT` sites (dataset commit, experiment commit,
   model provider acquire) — test-only arming; each takes the real failure
   branch with explicit rollback.
3. **Bug fixes found by the new verification layer (product-hardening):**
   - `RemoteRangeCache` passed a static-storage handler to
     `VSIFileManager::InstallHandler`, but `RemoveHandler` (GDAL ≥ 3.9)
     DELETES it → double free at static destruction. Now heap-owned
     (standalone repro aborts on old scheme, clean after fix).
   - `worker_protocol::parseFrame` threw `Json::LogicError` on malformed
     frames (`{"v":{}}`, array roots) — reachable from worker/host IPC
     paths; now refuses cleanly (`isObject` + `isInt` gates).
   - `IpcChannel::Options` default member initializer broke newer-Clang
     compiles (default argument ODR-use inside the enclosing class).
   - Dataset/experiment stores: real commit-failure branches now roll back
     explicitly (failed COMMIT can leave the SQLite transaction — and its
     write lock — open; same convention as `deleteDataset`, #774).
4. Release hygiene: ~5 MB of accidentally committed MSVC artifacts
   (`*.obj`, `vc140.pdb`) removed; `.gitignore` extended.

## New verification tooling and tests

- `scripts/verification_ladder.py`: L0–L8 layered lanes (compile guards →
  unit → contract/known-answer → integration → portability → stress →
  visual → benchmarks → full sweep), resumable per item with binary+
  commit identity stamps, machine-readable JSON, TEST_INFRA env governance,
  bounded parallelism/timeouts, offline only.
- `scripts/collect_readiness.py`: machine+human release-readiness report
  (`docs/verification/READINESS.{json,md}`); `not-run`/`not-built` are
  distinct verdicts, never pass.
- New suites: header self-containment probes (L0, failure-class F1 guard),
  `test_portability_contract` (F2 guard), `test_trace_chain_8` (chain
  correlation + store fault contracts), `test_known_answer_corpus_8`
  (grid window/block algebra + split formulas), `test_contract_fuzz_ipc`
  (worker frames, plugin manifests, split configs),
  `test_contract_fuzz_ops` (operator schemas, parameter projection, model
  manifests), `benchmark_scale8` (1k/10k/100k scheduling, 100k dataset
  metadata, window reads, trace file-sink overhead), fault8 case in
  `test_model_failure_matrix`.

## Compatibility / migration

No product semantic changes; trace is default-off; fault points are
test-only. New public header `geospatial/util/gdal_compat.h` documented in
`docs/verification/PLATFORM_MATRIX.md`.

## Test evidence (executed locally; online CI/CD not required and not awaited)

Host: Linux 6.18, Clang 22.1.8 Release, GDAL 3.13.3, 16 cores (two other
8.0 track agents built concurrently — numbers carry that noise).
- Build of all lane targets: OK.
- Ladder L0–L7: L0/L1/L2/L4/L5 core + all new suites PASS (exact per-item
  verdicts in `.planning/verification-platform-8/ladder.json` and
  `docs/verification/READINESS.md`).
- Pre-existing local failures recorded honestly (NOT introduced here, NOT
  masked): `io_range_cache` (1 assertion on clang/GDAL 3.13; aborts before
  the range-cache fix), `io_remote_range`/`io_remote_validator` (networking
  lanes hang in this sandbox), `mapspec_visual` (one #784 case),
  `test_exprs_ipc` flaky-first-run (passes on rerun). Each is owned by
  another track's seam; listed in the readiness caveats.
- Benchmarks: `benchmarks/quality7.json` + new `benchmarks/scale8.json`
  (evidence artifacts, never gates).

## Adversarial review and remediation

Two read-only adversarial reviews (architecture/correctness; tests/perf/
portability) — `REVIEW_LOG.md`: 1 P0 (compat header self-containment),
3 P1 (benchmark median validity, parseFrame crash path, lane coverage),
8 P2, 8 P3 — all P0/P1/P2 fixed; remaining P3s fixed or explicitly
accepted with rationale in the log.

## Known limitations / follow-ups

- Windows/MSVC + macOS stay documented-but-not-executed on this Linux host.
- STAC pagination fuzzing deferred (io-track-owned seam in `src/app`).
- `io_range_cache`/`io_remote_*` local failures need io-track triage on
  clang/GDAL 3.13 hosts; GCC-in-CI remains the authoritative lane.
