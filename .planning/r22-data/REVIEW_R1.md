# R1 Independent Adversarial Review — r22-data (commits 44f067a709, d3f8667bb7, b8cb89844f)

Reviewer: R1 (independent, no development context). Base: origin/master 911e0b17.
Scope: #1679 (P0 SSRF), #1700 (P1 disk-cache geometry), #1701 (P2 publish journal), #1680 (P2 pool race), #1702 (P2 promotion provenance), #1703 (P3 cleanup).

Verification performed beyond reading: built and ran `test_r22_data_security_consistency` (87/87), `test_io_remote_validator` (232/232), `test_io_range_cache` full file incl. the new #1700 case (323/323), `test_atomic_fs_caller_contract` (287/287), `test_io_raster_contract` (2094/2094 with `PROJ_DATA` set; 2 failures without it are environment-only, missing proj.db), `test_r15_B_geodata_persistence` (8308/8308), `test_r12b_e2e_durability` (271/271), `test_platform7_library` promotion cases (69/69), `test_data_platform_surface` (154/154).

---

## 1. #1679 — isPrivateHost authority parsing (offline_gate.cpp) — CORRECT, no blocking findings

The new URL branch (`src/geospatial/remote/offline_gate.cpp:517-536`) cuts the authority at the FIRST `/?#` and only then applies the LAST-`@` userinfo split. This matches RFC 3986 §3.2 and — decisively — libcurl's urlapi behavior, which is the connector this gate serves. It also now structurally matches `urlHostOf()`/`urlAuthorityOf()` (http_fetch.cpp:457-483).

Counterexamples constructed and traced:

- `http://127.0.0.1?@example.com/` (the issue shape): authority = `127.0.0.1`, no `@` inside it → private. Old code stripped at the first `@` (in the query) → `example.com` → public. Hole closed.
- `http://169.254.169.254#@example.com/latest`, `http://[::1]#@example.com/x`, `http://[::ffff:127.0.0.1]?@example.com/x`: all terminate at `?`/`#` before the `@` → private. Correct.
- Encoded `@` in userinfo, `http://admin%40@127.0.0.1/`: last literal `@` wins → host `127.0.0.1` → private. Matches curl (curl also splits on the last literal `@`; `%40` is not a split point).
- `http://a@b@127.0.0.1/`, `http://example.com:80@127.0.0.1/`: `rfind('@')` → host `127.0.0.1` → private. Matches curl's last-`@` rule.
- IPv6 with port, URL form `http://[::1]:8080/x`: authority `[::1]:8080` → bracket strip (`:548-552`) → `::1` → private. Bare `[::1]:8080`: else-branch (`:538-544`) → same path. Both retained.
- Empty authority `http:///path`, `http://`: authority `""` → host empty → `lowered.empty()` → return true (`:570-571`) — fails closed. Good.
- `@`-only-in-query, `http://example.com/?x=@127.0.0.1`: term at `?` → host `example.com` → public; curl also dials example.com. Consistent.
- Query-side empty host after split, `http://user:pass@?@host/`: authority `user:pass@` → host `""` → true (fail closed); curl rejects the malformed authority. Fine.
- Scheme-in-path (e.g. `example.com/?redirect=http://127.0.0.1/`): both old and new code take the FIRST `://` occurrence — behavior unchanged from master, and it errs toward blocking/failing resolution, not toward the connector disagreeing. Pre-existing quirk, not a regression.
- Backslash forms (`http:\\\\127.0.0.1\\`): no `://` → bare-host branch → colon strip yields `http` → unresolvable → false (public per gate). Pre-existing on master, unchanged by this diff. RED_GREEN's supplementary adversarial run (16 shapes) confirms curl's urlapi also does not treat `\` as an authority terminator, so gate and connector agree; the residual risk is a WHATWG-style consumer, which this codebase does not have (transport is libcurl/GDAL VSI only).

Non-URL/bare-host inputs: the else-branch still does NOT strip userinfo, so a bare `public.example.com@127.0.0.1` yields an unresolvable string → `getaddrinfo` fails → false (public per gate). This is unchanged from master and no production caller passes `user@host` without a scheme (callers pass URLs — process_network_policy.cpp:160/176, remote_source_validator.cpp:370/465, range_cache.cpp:1077/1969/2056, http_fetch.cpp:910 — or a parsed host — stac_client.cpp:267). P3 residual note only.

The DNS-resolution IPv6 branch (`:611-626`) now delegates to `isPrivateIpv6()` — closing the previously hand-rolled subset that missed `::`, multicast `ff00::/8`, NAT64 `64:ff9b::/96`, and IPv4-compatible forms. Strictly tighter, one rule in one place.

`resolveForPinning` literal branch (`:664-683`): under the process policy, private v4/v6 literals now return `PrivateAddress` instead of `NoResolutionNeeded`; with the policy OFF the verdict is unchanged (`NoResolutionNeeded`), which the new test pins (`test_r22_data_security_consistency.cpp:132-137`). This is fail-closed defense-in-depth; the only production caller (http_fetch.cpp:695, guarded transport) previously refused these requests anyway via the redirect guard with a different message. No compatibility break found.

Minor divergence noted (P3, pre-existing): `isPrivateHost` percent-decodes the host (`:546`) while `urlHostOf` does not; all divergence directions I could construct end in either both-sides-refusal or both-sides-failure-to-resolve (fail closed), so no exploitable gap.

**Verdict: fix correct. No blocking findings.**

## 2. #1700 — RangeDiskBlockStore geometry key (range_cache*.cpp) — fix CORRECT; integration test is a VACUOUS oracle (P2)

Code:

- `identityBasis` (range_cache_disk.cpp:298-316) now appends `\nbs=<blockSize>`, so a 64 KiB-layout block 1 (`[64K,128K)`) and a 128 KiB-layout block 1 (`[128K,256K)`) can never share a key. Both call sites (`diskBasis` range_cache.cpp:736-745, `putRunToDisk` :816-818) pass the caller's captured `blockSize`. All `identityBasis` callers in the tree were updated (grep: 2 src + 3 test call sites — no stale signature remains).
- Belt-and-braces `data.size() > blockSize → skip` in `loadBlocksFromDisk` (:779-785) is sound: short blocks are legal (EOF/range-end clamp), oversized blocks are impossible under a correct basis and are refused rather than inserted. `data.empty()` is unreachable (parseBlock rejects `dataSize==0`) — harmless defense.
- `updateConfig` (:879-900) drops memory entries (generation bump) and clears the disk store on a geometry change, with `clear()` deliberately outside `mMutex` to preserve the mMutex→disk-mutex ordering. I verified no path takes the disk mutex then `mMutex` (putBlock/readBlock are called with `mMutex` released), so no lock-order inversion.

Thread-safety questions asked of this review:

- Can a fetch that captured the old basis still write old-geometry blocks AFTER `clear()`? Yes, in one narrow interleaving: `putRunToDisk` validated `entry->generation` and captured the basis under `mMutex` BEFORE the drop, then does its `putBlock` loop after `updateConfig` finished (including `clear()`). Those files land under the old-geometry basis. Harmful? No wrong bytes can ever be served: under the new geometry the basis string differs (unreachable), and if a later config restores the old blockSize the files are content-correct (strong-ETag basis + per-block sha256 over basis||index||data + generation-validated at capture). Cost is dead bytes against the disk cap until LRU eviction — P3, acceptable for a cache lane.
- `insertBytes` (:709) re-checks `fetchConfigGeneration != configGeneration` under the lock, and `loadBlocksFromDisk` re-checks config+entry generation per block (:788), so neither stale memory inserts nor stale disk→memory promotion can happen after the swap.
- NEW TOCTOU introduced by the fix: `updateConfig` now compares `blockSize` in a first lock scope and applies in a second (:881-891). If two `updateConfig` calls interleaved, the second could skip `dropAllLocked()` while effectively changing geometry (stale-compare). In production this is unreachable: the only caller, `RemoteRangeCache::install` (range_cache.cpp:2241-2251), holds `g_storeLifecycleMutex` across `updateConfig`, and `uninstall`/`clearEntries` never call it. P3 latent-hazard note: the compare could simply be computed inside the single apply lock (same result, atomic).

**P2 finding (test/evidence, non-blocking for the code): the new integration test does not exercise the bug.**
`tests/test_io_range_cache.cpp:936-1021` ("a blockSize change never serves stale disk bytes (#1700)") uses `InstalledCache`, whose destructor calls `RemoteRangeCache::uninstall()` (test:108), and `uninstall()` unconditionally calls `RangeDiskBlockStore::clear()` (range_cache.cpp:2283/2291 — unchanged from master, verified via `git show 911e0b17`). Therefore at the start of Pass 2 the disk directory contains ZERO block files: the 128 KiB read necessarily refetches from the fixture server and trivially matches `expected`. Pass 2 and the "cold process" Pass 3 would both PASS on pre-fix master. Consequently the claim in `.planning/r22-data/RED_GREEN.md` (#1700 section) — "master 上…`loadBlocksFromDisk` 以同 basis 命中 64KiB 布局的 `<h>.1.blk`" — is factually wrong: those blocks were unlinked by `uninstall()` before Pass 2 began. The only genuine oracle in this PR for #1700 is the `identityBasis` string-format assertions (`test_io_range_cache.cpp:997-1026`, `bs=65536` vs `bs=131072`), which do fail on master and do pin the key separation. The wiring (blockSize plumbed into both call sites) is verified by reading, not by test. Recommended follow-up: a direct `RangeDiskBlockStore`-level test (putBlock under a 64 K basis, readBlock under the same URL+ETag with a 128 K basis must miss; plus the oversized-block refusal), which does not route through install/uninstall — or persist the disk dir across the passes without `InstalledCache`. Not blocking: the code fix is correct and the basis-format regression guard is real.

## 3. #1701 — atomic_fs publish journal crash consistency — CORRECT, no blocking findings

- `writeJournalFile` (atomic_fs.cpp:398-405) now throws on `!out.good()` after close, BEFORE any target is modified. A truncated journal can still exist on disk after the throw, but the next `recoverDatasetGroup` deletes it (parse failure) and no targets were touched; readers fail closed via `throwIfInterruptedPublish` in the interim. Strictly better than master (which proceeded to publish with no rollback record).
- Stale-`.bak` sweep (:686-695 group, :841-845 members) runs AFTER `recoverDatasetGroup` and BEFORE `writeJournalFile`. Within one process this is sound: after recovery no live journal exists, and recovery itself has consumed every live `.bak` (restored via rename, or removed). The only `.bak`s that remain are orphans of journals that were deleted as unparseable — exactly the residue that would otherwise be "restored" over current good bytes on a crash between `writeJournalFile` and `backupInPlace`. I traced the crash windows: (a) crash after journal write, before any `backupInPlace` → recovery finds `hadTarget=true` but no `.bak` → keeps the current (still-old, unmodified) target bytes — correct; (b) crash mid-member-publish → `.bak`s that exist were all made by THIS run — provable pre-publish images. The sweep is what makes (a) safe.
- Commit-marker failure now throws with journal+backups kept (:794-804 group, :925-935 members). State at throw: all targets published, uncommitted journal + fresh backups on disk, staged files consumed by the renames (no staged-file leak). The next `publishStagedGroup`/`publishStagedMembers` starts with `recoverDatasetGroup`, which rolls the group back to the pre-publish image (`recoverFromJournalFile` restores main first, then members). Readers between the throw and recovery are refused by `throwIfInterruptedPublish` (vector/raster/convert/metadata/cog readers) — fail closed. On master the same failure silently deleted the backups and reported success, leaving a journal that the next recovery would use to DELETE freshly published `had_target=false` members — the half-destroyed state the fix describes. Real bug, correctly fixed.
- Cross-process concurrency: two processes publishing the SAME target were already unsynchronized on master (same journal path, interleaved renames); the sweep does not worsen that pre-existing unsupported case. P3 note only.

Test oracle quality: the marker-failure case (directory occupying the marker path → POSIX `rename(file,dir)=EISDIR`) and the stale-`.bak` case (directory occupying the sidecar target) are genuine oracles — I traced both against master semantics and they fail there (master's ignored rename → no throw; master's cleanup would rename ANCIENT over CURRENT). P3 note: the third case, "a journal path that cannot be written" (`test_r22_data_security_consistency.cpp:259-283`), exercises the PRE-EXISTING open-failure check (`if (!out) throw`, master :393-394), not the NEW `out.good()` ENOSPC path; that new path has no test (acknowledged hard to inject without a fault hook). The test file header's claim "a failed journal write is an error" is only half-pinned.

## 4. #1680 — ArtifactObjectPool concurrent same-digest put — CORRECT, no blocking findings

- Unique temp names: pid + process-wide atomic counter (`artifact_object_pool.cpp:148-152`) — unique across threads and pool instances in-process, across processes via pid. Closes the shared-tmp clobber.
- Lost-race branch (:183-198): `QFile::rename` refuses to replace an existing destination; if the rename failed AND the object now exists AND no injected fault, the loop continues; the top re-hashes the object — dedup acceptance for the healthy winner, drop-and-restage for a torn one. Convergence: exactly one thread can lose a given rename, and the loop-top `exists && digest==expected → break` terminates on the next iteration, so at most one extra iteration per racer. Livelock requires an adversary repeatedly re-tearing the object between remove and re-hash — the same unbounded-loop shape master already had in the R20-02 torn-object branch. Acceptable.
- Accounting (`m_cachedTotalBytes`): loser adds nothing (its tmp is removed and `.puttmp` files are excluded from `totalObjectBytes`, :405-406); winner adds exactly once; the torn branch subtracts only after a successful remove. No double-count on the new path.
- Spurious-failure residual: if a racer's `QFile::remove(objectPath)` loses to another thread's remove (returns false), that racer returns `nullopt` even though a healthy object lands — pre-existing, fail-closed, not a corruption.

Test oracle quality (P3): `two pool instances racing one digest` launches two threads with no start barrier, so on pre-fix code a run where one thread completes fully before the other starts would still show `successes==2` (false green). Eight rounds with fresh content make a systematic false green unlikely but not impossible; a `std::barrier`/atomic start gate at the copy step would make it deterministic. On fixed code the test is deterministic-green (verified locally).

## 5. #1702 — SamplePromoter ignores addSourceAssetsToDraft failures — CORRECT, no blocking findings

All four promotion call sites now check the Result and return `dataset.promotion_partial` diagnostics that truthfully state what was committed (`sample_promotion.cpp:269-279, 384-394, 531-541, 633-643`). `[[nodiscard]]` added on `DatasetStore::addSourceAssetsToDraft` (dataset_store.h:98); grep confirms every production caller is one of these four checked sites (no callers in src/cli, src/agent, or elsewhere). Behavior note (accepted trade-off, consistent with the file's existing addSamples-failure doctrine): the failure surfaces AFTER samples were written — the diagnostic says so; a caller that blindly retries the promotion re-writes samples. Same shape as the neighboring annotation loop, not a new defect.

Test is a genuine oracle: corrupting `manifest_json` via direct SQLite while `requireDraftVersion` still admits the draft makes `addSourceAssetsToDraft` fail at `DatasetManifest::fromJson`; on master the ignored Result yields success, so `REQUIRE(!broken.has_value())` fails there. Verified green here.

## 6. #1703 — validateWindow overflow + stepRow error conflation — CORRECT, no blocking findings

- `validateWindow` (raster_reader.cpp:215-221): `xEnd`/`yEnd` computed in int64; `RasterWindow` fields are `int` and the degenerate-size check (:209) already rejects non-positive width/height, so the int64 sum cannot overflow; comparison promotes `metadata.width` (int) correctly. `{xOff=1, width=INT_MAX}` is now refused. `test_io_raster_contract` (2094 assertions) green — no legitimate window newly rejected.
- dataset_store.cpp: all four sites (730, 817, 914, 1043 — addSourceAssetsToDraft / stageVersion / commitVersionImpl / deprecateVersion) return `dataset.store_query_failed` with the SQLite error only when `StoreStmt::stepFailed()` is true (error rc, not DONE — semantics verified in dataset_store_impl.h:43-55). The `not_found` path is unchanged for genuine absence. I grepped callers of these functions (src/cli/cli_dataset_commands.cpp:385 and tests): none branch on the `dataset.not_found` code specifically — all treat any failure uniformly, so no caller regression. Other `stepRow()` sites in the file without the new branch are different shapes (existence checks, loops with their own stepFailed handling at :426/:452 etc.) — out of the issue's stated scope; no inconsistency introduced.
- readMask: doc-only scope note in raster_reader.h — no behavior change; justified by the 6 existing test call sites.

## 7. Tests audit summary

| Test | Real oracle? | Notes |
|---|---|---|
| r22 #1679 authority parsing | YES | Fails on master (RED independently reproduced by the track; assertions match curl semantics) |
| r22 resolveForPinning literals | YES | Policy-ON assertions fail on master |
| r22 #1701 marker failure | YES | Master ignores the rename result → no throw |
| r22 #1701 stale .bak | YES | Master's cleanup restores ANCIENT over CURRENT |
| r22 #1701 journal unwritable | NO (new code untested) | Exercises master's pre-existing open-failure check; the new `out.good()` path is untested |
| r22 #1703 validateWindow | YES | Master's int overflow passes validation for the evil window |
| r22 #1680 pool race | WEAK (probabilistic RED) | No start barrier; 8 rounds mitigate; deterministic green post-fix |
| r22 #1702 promotion | YES | Master returns success on the broken link |
| range-cache #1700 integration (3 passes) | NO (vacuous) | `uninstall()` clears the disk between passes; would pass on master; RED_GREEN.md claim incorrect |
| range-cache #1700 basis format | YES | `bs=65536`/`bs=131072` string assertions fail on master |

## 8. File whitelist — PASS

`git diff --name-only 911e0b17..HEAD`: only `.planning/r22-data/*`, `src/data/artifact_object_pool.cpp`, `src/dataset/{dataset_store.cpp,dataset_store.h,sample_promotion.cpp}`, `src/geospatial/raster/{raster_reader.cpp,raster_reader.h}`, `src/geospatial/remote/{offline_gate.cpp,offline_gate.h,range_cache.cpp,range_cache_disk.cpp,range_cache_disk.h}`, `src/geospatial/util/atomic_fs.cpp`, `tests/{CMakeLists.txt,test_io_range_cache.cpp,test_r22_data_security_consistency.cpp}`. All within the allowed set.

---

## Findings register

| # | Sev | Location | Finding | Blocks? |
|---|---|---|---|---|
| F1 | P2 | tests/test_io_range_cache.cpp:936-1021; .planning/r22-data/RED_GREEN.md (#1700) | The #1700 integration test is vacuous (uninstall() wipes the disk between passes; passes on master) and RED_GREEN.md's claimed RED mechanism is factually wrong. Basis-format unit assertions remain a genuine guard. Rewrite as a RangeDiskBlockStore-level test or persist the disk dir across passes; correct the doc. | NO (code fix verified correct by reading + basis test) |
| F2 | P3 | src/geospatial/remote/range_cache.cpp:879-891 | updateConfig compare/apply split across two lock scopes; safe only because install() serializes under g_storeLifecycleMutex. Fold compare into the apply lock. | NO |
| F3 | P3 | range_cache.cpp putRunToDisk vs updateConfig clear() | Narrow window writes unreachable old-geometry blocks after clear() (dead bytes against the disk cap; never servable). | NO |
| F4 | P3 | offline_gate.cpp:538-544 | Bare-host (no scheme) inputs still do not strip userinfo; unresolvable → verdict "public". Pre-existing; no production caller passes such shapes. | NO |
| F5 | P3 | tests/test_r22_data_security_consistency.cpp:259-283 | Journal-write test exercises the pre-existing open-failure path, not the new out.good() ENOSPC path. | NO |
| F6 | P3 | tests/test_r22_data_security_consistency.cpp:333-380 | #1680 race test has no start barrier; probabilistic RED on pre-fix code. | NO |
| F7 | P3 | offline_gate.cpp vs http_fetch.cpp urlHostOf | Parser duplication (with percent-decode divergence) persists by design; comment cross-references; all divergence directions fail closed. | NO |
| F8 | P3 | atomic_fs.cpp publishStagedGroup/Members | Cross-process concurrent publish of one target remains unsynchronized (pre-existing); sweep adds no new hazard. | NO |

## Verdict: **APPROVE**

No blocking defects found in the production code. All six fixes are correct as claimed; regression risk is low and empirically checked (9 suites, ~12k assertions green). Required follow-up (non-blocking): F1 — fix the vacuous #1700 integration test and correct the RED_GREEN.md claim; recommended: F2, F5, F6.
