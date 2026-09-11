# BASELINE — audit of origin/master @ 322dfd3876 (2026-09-10)

## 1. Repository state

- Latest master: `322dfd3876c34ed62b42846598cacd711c8c91d6` — identical to the
  planning snapshot; no drift between planning and execution.
- Open PRs: **none**. Open issues: **none**.
- Remotes: `origin` (this repo), `itk-upstream` (vendored ITK; out of scope).

## 2. Last 30 merged PRs mapped to this track

| PR | Branch | Track ownership | Relevance to 8.0 |
|---|---|---|---|
| #835 | fix/ci-external-process-ns | SDK/CI | Post-merge failure class F3: `_WIN32` branch closed the `exprs` namespace; POSIX re-opened a nested scope. **Direct motivation for WP-A.** |
| #834 | fix/ci-range-cache-gdal | Geo/CI | Failure class F2: GDAL 3.8 (Ubuntu) vs 3.13 (Homebrew) VSI API drift in `range_cache.cpp`. **Direct motivation for WP-A.** |
| #833 | fix/ci-master-post-832 | CI | Failure class F1: missing standard-library includes in `remote_source_validator.cpp` + `diagnostic_report.h` (transitive-include drift). **Direct motivation for WP-A.** |
| #832 | feat/cartography-platform-7 | Cartography 7.0 | Other track. |
| #831 | feat/verification-observability-7 | **Verification 7.0 — direct predecessor of this track** | Built `src/runtime/observability/**` (trace, fault registry, diagnostic report), `tests/support/bounded_fuzz.h`, test_contract_fuzz_{io,lang,data,agent}, test_fault_{registry,matrix}, test_concurrency_stress, test_known_answer_corpus, test_trace_contract, test_diagnostic_report, asan_smoke_core, benchmark_quality7, docs/verification/*.md. **Reuse, do not rebuild.** |
| #830 | feat/plugin-isolation-runtime-5 | Plugins 7.0 wave | Owns plugin host/IPC seams. |
| #829 | feat/scientific-algorithms-7 | Scientific algorithms 7.0 | Owns algorithm kernels (known-answer subject matter). |
| #828 | feat/execution-plane-runtime-7 | Execution plane 7.0 | Owns ExecutionPlane/worker/cache seams. |
| #827 | feat/pi-spatial-scientist-harness-7 | Pi harness 7.0 | Owns harness/agent seams. |
| #826 | feat/professional-workbench-7 | Workbench 7.0 | GUI. |
| #825 | feat/model-runtime-multimodal-7 | Model runtime 7.0 | Owns model runtime seams. |
| #824 | feat/dataset-experiment-7 | Dataset/experiment 7.0 | Owns DatasetStore/ExperimentStore/splits. |
| #823 | feat/cloud-geospatial-io-7 | Cloud I/O 7.0 | Owns remote/range cache/STAC seams. |
| #822 | fix/resolve-open-issues-773-817 | Issue wave M1-M7 | 45 issue fixes + test_e2e_open_issues. |
| #821-#818 | 6.0 wave | Help/diagnostics, cartography 6.0, workbench UX 6.0, scientific data 6.0 | Other tracks. |
| #772-#761 | 4.x/5.0 wave | Older platform tracks | Merged history. |

## 3. Surviving remote branches

All `origin/feat/*` and `origin/zcode/*` branches correspond to **merged**
PRs above (verified by PR headRefName matching) — historical residue, not
divergent work. `itk-upstream/*` is the vendored ITK mirror. No live
competing 8.0 branches exist at execution time.

## 4. Existing verification infrastructure (REUSE list)

- `src/runtime/observability/`: `trace.{h,cpp}` (TraceEvent, Ring/File sinks,
  `exp.trace.v1` NDJSON, disabled-by-default hot path), `trace_id.h`
  (26-char Crockford ULID-style ids), `fault_point.h` (`SICNU_FAULT_POINT`),
  `fault_registry.{h,cpp}` (NextN/Always/EveryNth, Armed RAII),
  `diagnostic_report.{h,cpp}` (`exp.diag.v1`), `execution_telemetry.*`.
- Trace adapter sites today: `execution_plane.cpp` (submit + completion),
  `job_engine.cpp` (operator job start/end), `src/app/main.cpp` (env
  bootstrap). That is the WHOLE chain wiring — workflow, task center,
  output committer, dataset/experiment seams are NOT adapted yet.
- Fault-point sites today: exactly 5 — `output_committer.publish`,
  `workflow_checkpoint.write`, `workflow_checkpoint.publish`,
  `artifact_pool.stage_copy`, `artifact_pool.stage_publish`.
- `tests/support/bounded_fuzz.h`: xorshift-based BoundedRandom, caps, mutate.
- Tests: test_trace_contract, test_fault_registry, test_fault_matrix,
  test_contract_fuzz_{io,lang,data,agent}, test_concurrency_stress,
  test_known_answer_corpus, test_diagnostic_report, asan_smoke_core,
  benchmark_quality7 (JSON out), test_perf_benchmarks + scripts/
  run_perf_baseline.sh + scripts/benchmark_harness.py (perf lanes).
- Docs: docs/verification/{TRACE_ARCHITECTURE,FAULT_MATRIX,KNOWN_ANSWER_MATRIX,
  PLATFORM_EVIDENCE,FLAKY_REPORT}.md.
- Build presets: dev-default, ci-fast, ci-full, sanitizer-debug,
  release-package. Local reusable build trees in the main worktree:
  `build/` (GCC Release Ninja), `build-clang/` (Clang Release Ninja).

## 5. Post-7.0 failure classes (the motivation for this track)

- **F1 transitive-include drift**: master failed to compile after the 7.0
  wave because two TUs relied on transitive includes (#833).
- **F2 GDAL version drift**: `range_cache.cpp` used VSI APIs that moved
  between GDAL 3.8 and 3.13; fixed with in-file `#if GDAL_VERSION_NUM`
  ladders (#834).
- **F3 platform-conditional code rot**: `_WIN32` branch of
  `sdk/exprs/external_process.cpp` broke the POSIX build via namespace
  structure; only caught because CI compiled Linux after merge (#835).

Common root cause: **portability decisions are scattered inline per TU, and
no local lane compiles more than one compiler configuration before merge.**

## 6. Accidentally committed artifacts (release hygiene)

PR #831 committed Windows build artifacts to the repo root:
`asan_smoke.obj`, `diagnostic_report.obj`, `fault_registry.obj`,
`trace.obj`, `vc140.pdb` (~5.3 MB). `.gitignore` covers neither `*.obj`
nor `*.pdb`. Removal + ignore rules are in scope for 8.0 release
engineering.

## 7. Local environment (evidence host)

- Linux 6.18 LTS x64, GCC 16.2.1, Clang 22.1.8, CMake 4.4.3, Ninja,
  GDAL 3.13.3, 16 cores, 62 GB RAM.
- **Not locally executable**: Windows/MSVC, macOS/AppleClang. Claims for
  those platforms are static (code-level) only, and the platform matrix
  must say so.
