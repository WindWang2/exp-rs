# Dynamic Baseline: exp-rs R7 Execution Plane Concurrency & Admission Closure

**Date**: 2026-10-01  
**Milestone**: M0 (Baseline & Independent Worktree Setup)  
**Author**: Worker Baseline Setup (`worker_baseline_m0`)  
**Target Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r7-execution-plane`  
**Target Branch**: `hardening/r7-execution-plane-concurrency`  
**Base Commit**: `origin/master` (`1e28de86772b75e35df1ce579ff977f446bbed4d`)  
**Read-Only Master**: `/home/kevin/projects/rs-studio/main` (`eac910dff92313b58684960623ac2cbcc71a2c47`)  

---

## 1. Repository State & Dynamic Baseline

### 1.1 Commit Hashes & Branches
- **`origin/master` SHA**: `1e28de86772b75e35df1ce579ff977f446bbed4d`
  - Commit message: `fix(tests): per-process isolation and lifecycle under one-case-per-process ctest (#1414)`
  - Branch tracking: `hardening/r7-execution-plane-concurrency` is configured to track `origin/master`.
- **`main` master SHA**: `eac910dff92313b58684960623ac2cbcc71a2c47`
  - Commit message: `Merge pull request #1294 from WindWang2/fix/ci-conflict-markers-1293-merge`
  - Status: Strictly read-only. Zero production code changes made on `main`.
- **Clone Topology**: Shallow git repository (`.git/shallow` lists boundary SHAs including `1e28de8677`). Worktree created cleanly via `git worktree add -b hardening/r7-execution-plane-concurrency /home/kevin/projects/rs-studio/exp-rs-r7-execution-plane origin/master`.

### 1.2 Inspected Open Pull Requests
Probed via `git ls-remote origin "refs/pull/*/head"` and local worktree checkouts:

| PR # | Remote Ref / SHA | Branch Name | Key Changes & Cross-Reference to R7 |
|---|---|---|---|
| **#1418** | `refs/pull/1418/head`<br>`e10035733730d37b037997b2069e2da020eb3ca3` | `hardening/r6-test-determinism-economics` | Replaced unbounded busy-wait spin loops in `test_execution_plane_8.cpp` and `test_task_center*.cpp` with bounded 60s waits via `bounded_wait.h`. Direct input for R6 deterministic test harness. |
| **#1423** | `refs/pull/1423/head`<br>`779f2480a7b6d1a4cf46009927c92f98024d6be9` | `fix/test-drain-flakes` | Addressed 10k rapid short-job saturation in `test_execution_plane_7.cpp` (bypassing 4096 pending queue bound to avoid -1 refusal) and observer polling races in `test_execution_plane_9.cpp`. Direct input for R4 admission & backpressure. |
| **#1424** | `refs/pull/1424/head`<br>`3b821d0dae78b7710b8fbbd95990ff05f3b0002c` | `fix/memory-guard-rss-sanitizer` | Hardened memory guard RSS checks under AddressSanitizer and LeakSanitizer. Informs Agent C sanitizer runs. |
| **#1426** | `refs/pull/1426/head`<br>`0e19b2c5b21d28a7ab746159c0439f730c62c6d6` | `fix/asan-lifetime-findings` | Fixed object lifetime and destruction ordering in UI, plugin, and worker runtime layers. Informs Agent B lifetime contracts. |
| **#1427** | `refs/pull/1427/head`<br>`e611fde745c0b33f8628c2e9abe9287849f273da` | `fix/plugin-host-restart-counter` | Plugin host restart counter state and diagnostics synchronization. |

---

## 2. Resource & Compilation Disciplines

All agents, build commands, and test suites must strictly adhere to the following execution constraints:

1. **Parallelism Bounds**:
   ```bash
   export CMAKE_BUILD_PARALLEL_LEVEL=2
   export CTEST_PARALLEL_LEVEL=2
   ```
2. **Build Invocations**:
   ```bash
   cmake --build <build_dir> -j2 --target <target_name>
   ```
   **Strict prohibition**: Never use `-j$(nproc)`, `-j4`, or unconstrained compilation. Always use `-j2` (or `-j1` when memory pressure dictates).
3. **Test Invocations**:
   ```bash
   QT_QPA_PLATFORM=offscreen ctest --test-dir <build_dir> -j2 --output-on-failure
   ```
4. **Sanitizer Runs**:
   ASan, UBSan, and stress harness runs must execute under `-j1` or `-j2` strictly.
