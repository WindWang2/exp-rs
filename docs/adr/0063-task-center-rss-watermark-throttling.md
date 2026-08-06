# ADR 0063: TaskCenter RSS Watermark Throttling

## Status
Accepted

## Context
TaskCenter gated task launch only by **static profile concurrency counts**
(`m_profileLimits` + `m_globalConcurrencyLimit`): e.g. "InProcess = hardware_concurrency-1,
CLI/Python = min(2, …)". These are fixed integers set at startup and never change
while tasks run. There was **no awareness of actual memory use** - a process running
several large-raster jobs could climb toward OOM with nothing holding back the next
queued task. The only system-memory API in the tree (`QgsApplication::systemMemorySizeMb`,
wrapping GDAL's `CPLGetUsablePhysicalRAM`) was dead code with zero callers, and no
`getrusage` / RSS sampling existed anywhere.

The goal (task 2.1) asks for dynamic CPU/RAM/GPU throttling. Investigation showed
the project does **no GPU computation** (OpenCL utils are display-only), so VRAM is
out of scope. CPU concurrency is already bounded by the profile counts. The acute
gap is **RAM**: preventing OOM when large rasters are in flight.

## Decision
Adopt a **process RSS + watermark** strategy (chosen over per-task declared-memory
budgets): sample the process resident set size and, when it reaches a watermark,
stop launching new tasks until RSS falls.

1. **`ResourceMonitor`** (`src/processing/framework/resource_monitor.h/.cpp`, in
   `sicnu_task_center`): a small class that samples RSS via
   `getrusage(RUSAGE_SELF)` on Linux (KB) / macOS (bytes), returns 0 elsewhere, and
   compares against a configurable watermark. Default watermark = 75% of system RAM
   (`QgsApplication::systemMemorySizeMb`); 0 disables the gate so a misconfigured
   environment never hard-blocks all launches. The sampler is injectable
   (`setRssSampler`) so tests can drive pressure without allocating real memory.

2. **TaskCenter integration**: `setMemoryLimitMb` / `memoryLimitMb` / `setRssSampler`
   delegate to an internal `ResourceMonitor` member. In `processNextQueuedTasks`,
   after the per-profile count check and before staging a task Running, a
   `memoryPressureHigh()` check **breaks** the launch loop (not `continue`):
   memory pressure is global, so the remaining eligible tasks cannot run either.

3. **Re-evaluation is automatic**: `markTaskCompleted` / `markTaskFailed` /
   `markTaskCanceled` each call `processNextQueuedTasks()`, so when a running task
   finishes (freeing memory) the gate is re-checked and a previously blocked Queued
   task can launch. No timer or explicit re-poke is needed.

4. `resetResourceProfileLimits` now also re-constructs the `ResourceMonitor`
   (restoring the default watermark + sampler), so tests get a clean slate.

## Consequences
- Large-raster pipelines no longer march toward OOM unopposed: once RSS crosses 75%
  of system RAM, new tasks wait until in-flight ones release memory.
- The gate responds to **current** RSS (see Amendment), so it reopens promptly
  when a running task frees memory - not only when the all-time peak finally
  recedes (which under a peak sampler it never would).
- `break` (not `continue`) means a high-priority queued task will not preempt the
  gate even if a lower-priority one is ahead of it - fairness is secondary to not
  OOMing. Priority still orders launches once pressure clears.
- Linux/macOS get real current RSS; Windows returns 0 (gate disabled, falls back
  to the existing count-based throttling). The project's target platform is Linux.
- Tests inject a fake sampler to deterministically assert hold/release without
  allocating gigabytes; two new `test_task_center` cases cover the hold-then-release
  flow and the disabled (limit 0) path, plus a `[rss]` case pinning the
  current-not-peak semantics of the default sampler.

## Amendment (VmRSS) - current RSS, not peak
**Status:** Supersedes the sampler portion of the original Decision (item 1).

The original `defaultRssMb()` used `getrusage(RUSAGE_SELF).ru_maxrss`, the
**all-time peak** RSS. That value is monotonic - it never decreases while the
process lives - so once the process had hit a memory spike the watermark gate
stayed closed forever, even after the memory was freed. This defeated the
"reopens when a running task finishes and frees memory" behavior the gate
exists to provide.

`defaultRssMb()` now reports **instantaneous current RSS**:
- **Linux:** `/proc/self/status` field `VmRSS:` (kB -> MB).
- **macOS:** `mach_task_basic_info.resident_size` (bytes -> MB).
- **Other/Windows:** 0 (gate disabled, unchanged).

This makes the gate reopen as soon as memory is actually freed. The
injectable-`setRssSampler` test contract is unchanged (tests still drive a
fake sampler), and the watermark math (`memoryPressureHigh()` = current >=
limit, 0 disables) is unchanged. The original Decision item 1's "samples RSS
via `getrusage(RUSAGE_SELF)`" wording is replaced by the current-RSS sources
above. A `[rss]` test pins the behavior: allocate ~128 MiB, observe RSS rise,
free it, assert RSS falls back below the high-water mark (impossible under a
monotonic peak sampler).

## Future
- GPU/VRAM monitoring (only if GPU compute is introduced).
- Per-task memory budget declaration + soft scheduling for finer control.
- UI exposure of live RSS / watermark on the task panel.
