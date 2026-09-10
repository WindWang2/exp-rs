# Fault Matrix — Verification / Observability 7.0

Deterministic fault injection across module seams. Every entry: the fault,
the injection seam, the expected contract, and the test that proves it.
**All hooks default to no-ops** (`SICNU_FAULT_POINT` → one relaxed atomic
load when nothing is armed); arming happens only from test code.

| Fault | Seam / injection | Expected (contract) | Test evidence |
|---|---|---|---|
| staged copy fails (disk full analog) | `artifact_pool.stage_copy` | `put` returns nullopt, tmp removed, pool stays healthy, next put succeeds | `test_fault_matrix` (pool staging copy failure) ✓ |
| staged publish (rename) fails | `artifact_pool.stage_publish` | nullopt, no object fabricated, no `.puttmp` residue | `test_fault_matrix` (pool publish-rename failure) ✓ |
| mid-group publish rename fails | `output_committer.publish` (EveryNth) | commit fails truthfully, whole group rolled back, no half-published dataset | `test_fault_matrix` (mid-group rollback) ✓ |
| in-place commit failure | not probed — `isInPlace` commits take a different branch | acceptable: in-place publish has no staging/rollback window | note (review R1) |
| publish rename fails (lock/permission) | `output_committer.publish` (NextN) | commit fails, previous stable output survives byte-identical, no `.new`/`.old` residue | `test_fault_matrix` (publish preserves previous stable) ✓ |
| checkpoint write fails (short write / ENOSPC) | `workflow_checkpoint.write` | save returns empty, tmp removed, old checkpoint still loads | `test_fault_matrix` (checkpoint write failure) ✓ |
| checkpoint rename fails (locked target) | `workflow_checkpoint.publish` | save returns empty, old checkpoint bytes intact, no tmp residue | `test_fault_matrix` (checkpoint publish failure) ✓ |
| registry-mode faults (fail-once / fail-n / every-nth) | fault registry | exactly N firings under 8-thread concurrency; NextN rollback re-entry runs fault-free (true nested probe), Always re-fires by design; RAII survives throwing assertions | `test_fault_registry` ✓ |
| cache object corrupt (self-heal) | corrupt object bytes | corrupted object → cache miss, never wrong serve | `test_fault_injection` (POSIX) [existing] |
| checkpoint file corrupt | corrupt checkpoint JSON | skipped, not fatal; ghost-election prevents double execution | `test_fault_injection` (POSIX) [existing] |
| cross-process run lock | second owner | double-start refused, serialized submissions | `test_fault_injection` (POSIX) [existing] |
| worker crash before handshake | kill `sicnu_worker` process | typed "worker crashed" task failure, host survives | `test_worker_host` [existing, portable] |
| worker unresponsive | cancel escalation | cancel path terminates worker after grace | `test_worker_host` [existing, portable] |
| worker pool lifecycle | broken program / lifetime budget | typed failure, recycling, shutdown refusal | `test_worker_host` [existing, portable] |
| HTTP timeout / truncate / changed ETag | `src/geospatial/remote/*` + `tests/support/http_range_server` | validator rejects, cache never serves stale-as-fresh | **owned by feat/cloud-geospatial-io-7** (dedup, out of scope) |
| GDAL open/read error | invalid/truncated dataset files | typed operator/IO error, no crash | io track + `test_io_*` [existing] |
| model OOM / provider failure | model runtime failure matrix | typed failure, session released | `test_model_failure_matrix` [existing] |
| SQLite busy / corrupt | second exclusive connection / truncated db | busy → typed retryable failure; corrupt → store refuses, no silent data loss | planned M2b (test-side, no hooks needed) |
| plugin crash / unload | `sicnu_sdk` external process | typed failure; unload quiescence | partially POSIX-only (`test_exprs_external_process`) — Windows gap documented in task I |
| cancellation race (queued vs running) | JobEngine cancel under load | every job terminal; cooperative cancel observed | `test_concurrency_stress` (cancel race) ✓ |
| instant completion race | TaskCenter→engine dispatch | instant-completing job never strands a task in Dispatching | `test_concurrency_stress` (instant storm) + `test_job_engine` #799 cases |
| child job from worker | worker-thread `waitForJob` | rejected immediately (no deadlock), child still completes | `test_concurrency_stress` (#798) ✓ |
| shutdown during dispatch | `shutdown()` with jobs in flight | Cancelled records, no worker respawn | `test_concurrency_stress` (#684) + `test_worker_host` pool shutdown ✓ |
| layer removal during render | QGIS canvas lifecycle | no UAF; render threads fenced | platform-5.0 audit P0s fixed (#822); stress regression tracked in task E (needs GUI closure) |

Legend: ✓ = runnable now on Windows/MSVC; [existing] = pre-existing suite;
POSIX gaps and their disposition are tracked in the platform evidence matrix.
