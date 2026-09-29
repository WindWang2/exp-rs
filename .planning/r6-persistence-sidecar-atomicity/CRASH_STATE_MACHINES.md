# R6 CRASH STATE MACHINES — the sidecar authority and its migrated writers

Conventions: "old" = the previously published generation; "new" = the payload of
the in-flight write. Readers = `platform::sidecar::read` or direct main-file
reads. Temp residue = `"<stem>.<pid>.<counter>.<rng>.tmp<ext>"` claimed O_EXCL in
the target's own directory. INERT = no reader of any family ever globs or opens
these names (verified per family below).

## Authority `platform::sidecar::write` (all migrated writers share this)

| Crash point | Target state | Temp residue | Last-good | Verdict |
|---|---|---|---|---|
| before claim | old intact | none | old | clean |
| after claim, before write | old intact | empty INERT file | old | clean |
| mid temp write | old intact | partial INERT | old | clean |
| after temp write, before fsync | old intact | complete-but-unflushed INERT | old | clean (power loss may zero it — still INERT) |
| after fsync, before publish | old intact | fsynced INERT | old | clean |
| publish (rename/ReplaceFileW) | old-or-new, atomic | none (renamed away) | old | the only state flip |
| after publish, before last-good | new | none | old (one generation behind, decodable) | accepted: last-good never leads main |
| during last-good write | new | INERT at `<lastgood>` temp | old-or-new | clean |
| after last-good | new | none | new | fully rotated |

Failure paths (non-crash, deterministic): ClaimFailed / WriteFailed /
DurabilityFailed / PublishFailed all remove the temp and leave the target
untouched; StagedCleanupFailed reports unremovable INERT residue. Durability
refusal is deliberate: publishing unflushed bytes trades a typed error for a
torn artifact. Verify failure: main is new-but-suspect (typed VerifyFailed,
caller surfaces it); last-good holds the intended payload.

Read side (`read`): main Ok → Main; main missing/corrupt/oversize → last-good
Ok → LastGood (RECOVERED content); neither → Missing (both absent) / Corrupt.
Content decode + schema gates stay with callers: missing `schema_version` =
legacy (decode with legacy tolerance); future `schema_version` = fail-closed
refusal, never saved over (mission family, classification R6 gate).

"Process killed mid-phase" is exercised deterministically by phase faults
(WriteHooks::failAt + payloadOverride for torn bytes) asserting the same
post-states as the table — the equivalence is exact because every phase
boundary's observable state is what the fault test asserts (DECISIONS D6).

## Per-family crash windows (post-R6)

| Family | Writer (post-R6) | Old mechanism | Residual window | Residue sweeper |
|---|---|---|---|---|
| Classification `<raster>.class.json` | `RsPostProcess::saveClassMetaData` → authority + `.last-good` | plain open+truncate (tearing) | none beyond authority table | — |
| Mission authority sidecar | `saveMissionContextToSidecar` → authority | QSaveFile, no fsync (power-loss rename-then-torn) | none beyond authority table | mission_runtime_store rotateLastGood re-arms recovery |
| Mission last-good | `writeBytesAtomically` → authority | QSaveFile, no fsync | same | — |
| Legacy timeline | REMOVED (import-only, #1394) | — | writer cannot exist | — |
| Plugin user disable index | `PluginRegistry::saveUserIndex` → authority | ofstream+rename, no fsync, silent errors | same | — |
| Plugin discovery cache | `plugin_discovery::storeIndex` → authority | fixed shared `.tmp` (cross-process interleave) | same | self-heals as cache miss |
| CLI batch result index | `cli_batch_runner::writeResultIndex` → authority | fixed shared `.tmp`, torn ledger | same | — |
| Workflow checkpoint | `saveCheckpoint` → authority | tmp+rename; Windows lane without FlushFileBuffers | same; both lanes now fsync | recoverInterruptedRuns sweep |
| Harness sidecars (uncertainty/provenance/compile/verification) | `evidence::atomicWrite` + `writeCompileSidecar` → authority | QSaveFile, unchecked write | same | — |
| Session journal | `session_journal::writeFileAtomic` → authority | full parallel implementation | same (one authority deleted) | — |
| Teaching batch checkpoint/reports | `batch_assessment` → authority | QSaveFile lanes | same | — |
| Study report / studio export / registration report / lab reports | → authority | QSaveFile lanes, divergent error mapping | same | — |
| Processing outputs (multi-file) | `output_committer` per-pair → `fsyncFile + publishStagedFile` | copy→.new→rename, no fsync | kill between .old-rename and swap: target absent UNTIL next pair's failure restore runs; on kill, restore happens at next commit (documented #617 acceptance) | .old files restored/removed per commit |
| Dataset groups (shapefile/ENVI) | `atomic_fs::publishStagedGroup/Members` (unchanged, #1403-fresh) | — | sidecars-first-main-last contract | stage_ledger reconcile |
| Range block cache | `range_cache_disk::putBlock` — EXEMPT (cache-only, loss benign, lock-scope design) | — | miss | LRU/mtime sweep |

## Known accepted windows (documented, not fixed here)

1. `writeProvenanceSidecarIfAbsent` exists-guard is check-then-write (not
   O_EXCL on the target): two simultaneous first-writers can both publish; the
   last rename wins. An O_EXCL-on-target alternative would introduce a WORSE
   crash state (an empty claimed target file left by a kill between claim and
   publish). Unchanged by R6; engine+harness runs are the only writers and
   they are near-simultaneous by construction.
2. `output_committer` multi-file kill window (above) — pre-existing #617
   acceptance, now with fsync'd per-pair publishes.
3. ReplaceFileW's backup metadata durability on Windows is kernel-managed;
   the authority's durability gate is the pre-rename fsync of the staged
   bytes + MOVEFILE_WRITE_THROUGH (or ReplaceFileW's transactional swap).
