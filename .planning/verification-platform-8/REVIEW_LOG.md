# REVIEW LOG — Verification Platform 8.0

## Self-review pass 1 (pre-subagent), 2026-09-10

| Finding | Severity | Disposition |
|---|---|---|
| Workflow adapter duplicated effectiveStart computation | P3 | fixed: adapter moved to function tail, reuses the signal's value |
| Ladder lane named a WIN32-only target (test_exprs_external_process_win) | P2 | fixed: replaced with portable test_exprs_ipc (D5) |
| Ladder lane named a non-target (test_cartography_visual is a source of test_mapspec) | P2 | fixed: lane runs test_mapspec (D6) |
| collect_readiness referenced the same wrong names | P2 | fixed together with lanes |
| benchmark_scale8 scale list allowed 10000 > custom env max | P3 | fixed: dedupe + filter |
| fuzz_ops asserted properties as object (they are an array by design) | P2 | fixed: array + size check |
| fuzz_ipc embedded an .inc include inside TEST_CASE (invalid structure) | P1 | fixed: real TEST_CASE bodies |
| fuzz_ipc used jsoncpp for SplitConfig::fromJson which takes QJsonObject | P1 | fixed: Qt JSON parse path |
| model fault test used QString::absolutePath() (nonexistent) | P1 | fixed: plain path |
| model fault test missing fault_registry include | P1 | fixed |

## Self-review pass 2 (diff read-through), 2026-09-10

- Product-code inserts reviewed against the hot-path contract: every emit
  site gates on Trace::enabled() before string building; fault points sit on
  the real failure branch with explicit rollback.
- test_trace_chain_8: TraceScope/FaultScope RAII prevents cross-test leaks;
  TaskCenter singleton reset via shutdownForTests mirrors test_task_center.
- No edits to src/core/**, src/gui/** (vendored) — verified via diff paths.

## Adversarial review A — architecture/correctness (subagent 1/2), 2026-09-10

Reviewed the full diff vs 322dfd3876. Findings and dispositions:

| # | Finding | Sev | Disposition |
|---|---|---|---|
| A1 | gdal_compat.h NOT self-contained: cpl_port.h does not define GDAL_VERSION_NUM (it lives in gdal_version.h); cold-TU probe fails → would break the branch's own L0/L2 compile gates | **P0** | FIXED: `#include <gdal_version.h>`; comment corrected; cold-probe (with and without -I gdal) verified passing |
| A2 | experiment_store.cpp upsertRun wrapper uses std::chrono without <chrono> (transitive today; MSVC/libc++ risk) | P2 | FIXED: include added |
| A3 | fault branches roll back but the mirrored REAL commit-failure branches did not — inconsistent with the store's own #774 convention (failed COMMIT can leave the transaction active and leak the write lock) | P2 | FIXED: real branches in both stores now rollback explicitly; fault branch is literally identical to the real branch |
| A4 | trace status gaps: Cancelling indistinguishable from Canceled; Interrupted (terminal) carried no verdict; TaskCenter pending states carried neither status nor phase | P3 | FIXED: status vocabulary documented (ok/error/cancelled/cancelling/interrupted; phase start/pending/end) and applied at both adapters |
| A5 | global *.obj/*.pdb ignore would need a negation if Wavefront OBJ assets ever land | P3 | ACCEPTED: documented in .gitignore comment |

Clean checks reported by the reviewer: ownership/layer guards (no second
bus/scheduler/runtime; PRIVATE runtime links keep the science-core guards
green), locking discipline of every adapter (publish outside m_mutex at
TaskCenter; under-lock publish matches the pre-existing signal emit in
notifyRunStateLocked), wrapper pattern verbatim bodies (no disabled-path
behavior change, no double tracing), model fault point mirrors the real
typed failure before any provider work, all 7 migrated #if sites map to
identical preprocessor conditions, all new CMake references resolve.

(Adversarial review B — tests/performance/portability — pending after the
ladder run; subagent 2/2.)

## Adversarial review B — tests/performance/portability (subagent 2/2), 2026-09-11

| # | Finding | Sev | Disposition |
|---|---|---|---|
| B1 | benchmark_scale8 schedule repeats 2-3 were no-ops (submitWithId refuses reused ids; median picked a fake repeat) | **P1** | FIXED: unique id prefix per scale; repeats=1 (each scale measured once, honestly) |
| B2 | parseFrame NOT total: `frame["v"].asInt()` throws on non-numeric AND `isMember` throws on non-object roots — product crash path via worker_process_io.h/sicnu_worker_main.cpp | **P1** | FIXED in worker_protocol.h: `isObject() && isMember("v") && isInt() && asInt()==1` — malformed peer frames now refuse cleanly at the protocol gate (verified by fuzz: 3959 assertions green) |
| B3 | Ladder lanes never ran the branch's own new suites | **P1** | FIXED: fuzz_ipc + corpus_8 → L2, trace_chain_8 → L1, fuzz_ops → L4 |
| B4 | Readiness read bench filenames the ladder never wrote | P2 | FIXED: accepts both spellings |
| B5 | readiness "ready" verdict inflated | P2 | FIXED: ready ⟺ every named capability passed here |
| B6 | resume state fragile (no binary identity, written once) | P2 | FIXED: per-item persistence + sha/mtime identity stamps; rebuilt binaries invalidate recorded passes |
| B7 | portability test masked the GDAL<3.9 uninstall gap | P2 | DOCUMENTED in PLATFORM_MATRIX + test comment (local host is 3.13; the 3.8 lane stays a CI concern) |
| B8 | header-self-containment capability could never read "compiled" | P2 | FIXED: detect probe OBJECT files |
| B9 | --strict was a no-op | P3 | FIXED: default exit fails on failed/timeout; --strict also fails skipped/not-built |
| B10 | runner blind to multi-config generators; stale docstring | P3 | FIXED: config-dir candidates added |
| B11 | fuzz-ops baseline cleanliness never asserted | P3 | FIXED: CHECK(baselineProblems.empty()) |
| B12 | schema-builder fuzz checked shape not content | P3 | FIXED: properties[0].name/description == fuzzed values |
| B13 | fault8 leaked a catalog registration | P3 | FIXED: RAII-order unregister + count assert |
| B14 | TaskCenter chain test could hang behind the RSS admission gate | P3 | FIXED: bounded admissionSnapshot pre-check with honest WARN skip |
| B15 | IpcChannel::Options explicit ctor breaks aggregate init (SDK surface) | P3 | ACCEPTED: no in-repo aggregate usage; clang-22 pragma documented in header |
| B16 | PYTHONHOME venv caveat in governed_env | P3 | ACCEPTED as policy note (mirrors CTestCustom.cmake) |

## Local-holdings triage of first ladder run (pre-existing vs regression)

- worker_host fail → sicnu_worker binary not built in the lane target list;
  built + rerun → PASSES (12 cases, 56 assertions).
- portability_contract SIGABRT → REAL master bug found by the new contract:
  RemoteRangeCache passed a STATIC-storage handler to
  VSIFileManager::InstallHandler, but RemoveHandler (GDAL>=3.9) DELETES the
  handler → double free at static destruction. Standalone repro: aborts on
  master's scheme, clean after the ownership fix (heap-owned g_handler).
  FIXED.
- io_range_cache / io_remote_range / io_remote_validator / mapspec_visual:
  PRE-EXISTING on this host (clang 22 + GDAL 3.13.3 + sandboxed networking);
  my range_cache diff is a provably semantics-preserving macro rename
  (review A check E), and the io/visual suites exercise io-track/cartography
  code this branch does not modify. Recorded honestly as failed/timeout in
  the ladder JSON and readiness report — NOT marked pass.
