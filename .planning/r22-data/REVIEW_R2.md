# R2 Independent Adversarial Review — r22-data

Reviewer: R2 (second independent reviewer, no development context).
Reviewed range: `911e0b17..5fc544a6c6` (base origin/master 911e0b17).

**Scope note — the tree moved during this review.** At review start HEAD was
`f22ec241bc` (the R1-fix round). While R2 was tracing the #1701 sweep, the
developer landed `5fc544a6c6` ("orphaned .bak with absent target is
resurrected, never unlinked", 2026-10-26 02:09 +0800) plus an uncommitted
`.planning/r22-data/RED_GREEN.md` correction. This report pins `5fc544a6c6`;
the mid-round defect that commit fixes is finding **R2-1** below (it was a
genuine new defect of the R1-fix round, not present on master).

Verification performed beyond reading (binaries from the track's build tree,
`build-r22-data`, rebuilt by the developer at 02:09:29, i.e. from 5fc544a6c6
sources):

- `test_r22_data_security_consistency`: full file, 3 runs — 9/9 cases, 91
  assertions, all green.
- `test_r22_data_security_consistency [issue1680]`: 10 consecutive runs, 10/10
  green (race determinism post-fix).
- `test_io_range_cache`: full file — 39/39 cases, 327 assertions green;
  `[issue1700]` (2 cases) and `[issue1228]` green.
- `git show 911e0b17:src/geospatial/remote/range_cache_disk.cpp` — master
  `identityBasis` extracted and compared against the test's RED claim.
- Qt 6.12 empirical rename experiment (`QFile::rename(b, a)` over an existing
  `a`): **returns false, destination untouched** — the #1680 premise
  "QFile::rename refuses to replace an existing destination" is TRUE on this
  platform/toolchain (the `continue` branch is the live path on Linux, not
  dead code).

---

## A. R1-fix regression check (highest priority)

### A.1 — updateConfig / clear() lock ordering: NO inversion, NO regression

Traced every pairing of `CacheStore::mMutex` and the `DiskState::mutex` in
`disk()` (range_cache_disk.cpp:42-57):

- `configure` (:218-257), `enabled` (:259-264), `clear` (:266-289), `stats`
  (:291-296): take `state.mutex` alone; none call back into `CacheStore`.
- `readBlock` (:318-386): brief `state.mutex` holds for enabled-check,
  directory copy, corrupt accounting, hit accounting — the file read is
  outside all locks; **no path acquires `mMutex` while holding
  `state.mutex`.**
- `putBlock` (:393-481): name allocation under `state.mutex`, I/O outside,
  accounting under `state.mutex`; never touches `mMutex`.
- `CacheStore::updateConfig` (range_cache.cpp:879-903): the R1-F2 fix holds
  `mMutex` for compare+apply+`dropAllLocked`, **releases it, then** calls
  `RangeDiskBlockStore::clear()`. Not nested — the comment's invariant
  holds, and in fact no code path anywhere nests the two locks.
- `putRunToDisk` (:800-832) takes `mMutex` for basis capture, releases, then
  `putBlock` (disk mutex). `loadBlocksFromDisk` (:751-796) alternates — disk
  read unlocked, insert under `mMutex`.

Conclusion: deadlock impossible; the R1-F2 single-lock compare/apply is
semantically identical to master's under the only caller (`install()`, which
serializes under `g_storeLifecycleMutex`, range_cache.cpp:2249-2252) and now
also safe standalone. The only residual is R1's F3 (unchanged): a fetch that
captured the old basis before the swap can `putBlock` old-geometry files
*after* `clear()` ran — unreachable dead bytes only (basis differs), never
servable; `loadBlocksFromDisk` re-checks `fetchConfigGeneration`/
`entry->generation` per block (:788) so nothing stale enters memory. See
R2-3 for a sharper nuance about what `clear()` destroys.

### A.2 — store-level #1700 oracle: RED claim about master is CORRECT

`git show 911e0b17:src/geospatial/remote/range_cache_disk.cpp` — master's
`identityBasis` returns `"etag\n" + requestUrl + "\n" + etag` with no
blockSize anywhere. Therefore, on a geometry-blind variant of the new
signature, `basis64 == basis128` (same URL+ETag), `putBlock(basis64, 1,
bytes64K)` writes `<sha256(basis)>.1.blk`, and `readBlock(basis128, 1, ...)`
resolves the *same file name*, parses it (magic ok, index 1 == 1, digest over
`basisHash||"1"||data` recomputes over the identical basis hash) and returns
the 64 KiB-layout bytes → the test's `CHECK_FALSE(readBlock(...))` fails. The
store-level case (tests/test_io_range_cache.cpp:936-969) is a genuine oracle.
Two precision caveats, neither blocking:

1. The test as committed cannot compile against literal master (7-arg call vs
   master's 6-param signature). Its RED is against the geometry-blind-basis
   variant; the compile-independent proof on master is the basis-format
   assertion set in the `[issue1228]` case (bs=65536/bs=131072 strings).
   The corrected RED_GREEN.md now states exactly this. (R2-4.)
2. `InstalledDiskStore`'s destructor disables but does not wipe the store
   directory — the store-level case leaves `.blk` residue under
   `/tmp/sicnu_io_test_range_cache/disk_basis_split`; each run's `scratch()`
   `remove_all`s it first, so no cross-run contamination. Cosmetic only.

The integration case ("a blockSize change never serves stale-geometry disk
bytes", :971-1021) remains **vacuous in-process** — its own comment and the
corrected RED_GREEN.md now admit this (`InstalledCache`'s destructor →
`uninstall()` → `RangeDiskBlockStore::clear()` wipes the directory between
passes, so passes 2/3 always re-fetch from the fixture; it would pass on
master). Retained honestly as an end-to-end contract pin. (R1-F1 resolved.)

### A.3 — race-test start gate: no UB, no hang, low flake

`std::atomic<bool> go` + `yield()` spin, `go.store(release)` /
`go.load(acquire)` (tests/test_r22_data_security_consistency.cpp:356-371):

- **Hang:** impossible. It is a polling loop on an atomic, not a condvar —
  if `go` is already true when a thread arrives, the loop exits on the first
  check; there is no missed-wakeup shape.
- **UB:** none; both atomics are properly synchronized, `successes` is
   atomic, threads joined before `CHECK`.
- **20 ms sleep:** only a "park them first" courtesy. Under heavy load a
  thread may start after `go` is set — the test stays green post-fix
  (verified 10/10), it only weakens the RED on master (see R2-6: the RED is
  probabilistic, not "by construction" as RED_GREEN.md claims).

---

## B. Fresh looks

### B.4 — #1679: percent-decode order vs curl: fail-closed in every divergence I could construct

The gate order is: authority cut at first `/?#` → userinfo split at LAST
literal `@` (offline_gate.cpp:529-536) → `percentDecode(host)` (:546) →
bracket strip (:548-552) / single-colon strip (:553-556) → residual-`%`
truncation (:558-560). curl's urlapi: authority ends at `/?#`, userinfo split
at the last *raw* `@`, then the host is percent-decoded. Both sides decode
the same final host string; the divergences all fall on the side of the gate
being *stricter*:

- `http://%5B::1%5D/` → gate decodes to `[::1]` → `::1` → loopback →
  **blocked**. curl sees raw host `%5B::1%5D` with no raw brackets, a
  non-digit "port" after the last `:` → **malformed URL, no connection**.
  Block-vs-fail — safe.
- `http://127.0.0.1:80%40/` → authority `127.0.0.1:80%40`, no raw `@`; gate
  decodes → `127.0.0.1:80@` → exactly one colon → strip → `127.0.0.1` →
  **blocked**. curl: port token `80%40` is not digits → reject (and its host
  would be 127.0.0.1 anyway). Consistent or stricter.
- `http://example.com%3A127.0.0.1/` → gate: decode → `example.com:127.0.0.1`
  → single colon → strip → `example.com` → public → allow; curl decodes the
  host to a string containing `:` → **invalid hostname, no dial**.
  Allow-vs-fail — no SSRF (the connector never connects).
- `%00`-truncation shapes (`8.8.8.8%00%00%00`): gate's `parseIpv4` on the
  full std::string fails, `getaddrinfo(lowered.c_str())` stops at the NUL →
  judges `8.8.8.8` → public → allow; curl rejects embedded NUL in host.
  Allow-vs-fail.
- Zone-id `[fe80::1%25eth0]` → gate decodes, truncates at residual `%` →
  `fe80::1` → link-local → **blocked**; curl dials a link-local (private
  class). Gate stricter.
- Raw `@` split parity: last-`@` (gate :533) == curl. `http://a@b@127.0.0.1/`
  → host `127.0.0.1` → blocked (master's FIRST-`@` parse yielded `b@127.0.0.1`
  → unresolvable → public → the actual SSRF hole; closed).
- `http://127.0.0.1\@example.com/` → both sides treat `\` as non-terminator,
  host `example.com` — gate matches curl exactly (RED_GREEN's extended-input
  analysis is consistent with my trace).

No gate-allows/curl-dials-private shape found. The IPv6 DNS-branch
unification to `isPrivateIpv6` (:611-626) is strictly tighter (adds `::`,
ff00::/8, NAT64 64:ff9b::/96, IPv4-compatible — verified against
isPrivateIpv6's full list, :326-391). The `resolveForPinning` literal
self-judgment (:656-683) is fail-closed defense-in-depth and the
policy-OFF path is unchanged (pinned by test :132-137).

### B.5 — #1701 stale-.bak sweep: the R1 round DID introduce a data-loss defect (now fixed mid-review)

This was the critical adversarial question and the answer is **yes, R2-1**:
`f22ec241bc` swept every member `.bak` with an unconditional
`removeFileQuiet(member.backup)` placed **before** `writeJournalFile`.
`backupInPlace` (atomic_fs.cpp:299-311) falls back to move-aside
(`renameReplaceQuiet(target→bak)`) on hardlink-less filesystems — so a
previous run can crash with **target ABSENT and `.bak` holding the target's
only bytes**, its journal destroyed unreadable (recoverFromJournalFile
deletes unparseable journals without touching `.baks`, :424-434). In that
state the f22ec241bc sweep unlinked the only copy; if the new
`writeJournalFile` then threw (the very ENOSPC check that round added) or any
sidecar publish failed, the operator was left with **neither old nor new
bytes — permanent data loss not possible on master** (master's failure paths
never touch a `.bak` they didn't create; its success path replaces the
content anyway). R1 reviewed this exact sweep (REVIEW_R1.md §3) and called it
"sound", tracing only target-present residue — a missed shape.

`5fc544a6c6` replaces the unconditional sweep with resurrect-or-sweep
(atomic_fs.cpp:667-700 group, :855-870 members): `.bak` + target present →
remove; `.bak` + target absent → `renameReplaceQuiet(.bak→target)` — and it
runs **before** `hadTarget`/`hadMainTarget` are computed, so the journal's
`had_target` bookkeeping stays truthful. I traced the fix against
`recoverDatasetGroup` ordering (recover first, :662; sweep after), the
cleanup lambda (:724-773), and `recoverFromJournalFile`'s committed/
uncommitted branches — no path deletes a last copy anymore. The
directory-at-target sabotage edge (`fileExists` is false for directories,
:65-70, so a `.bak` facing a directory "target" attempts a doomed rename and
residues harmlessly) matches master's behavior. Fix correct; its test is not
(R2-2).

The commit-marker failure handling (:811-829, :918-935) and the
`writeJournalFile` `out.good()` check (:398-405) re-verified: throw with
journal+backups kept while targets are in a recoverable state; recovery
restores the pre-publish group; readers fail closed via
`throwIfInterruptedPublish` in the interim. Master's silent half-destroyed
state is gone. The `out.good()` path remains untested (R2-7).

### B.6 — #1680 continue-loop: converges; premise verified; counter is sufficient

- **Premise:** empirically verified with Qt 6.12 on this host —
  `QFile::rename` refuses to overwrite (`ok=0`, destination intact). The
  `QFile::exists(objectPath)` guard after a failed rename
  (artifact_object_pool.cpp:183-198) is therefore the live Linux path, and
  under replace-semantics platforms the code also converges (both renames
  publish identical, digest-verified bytes).
- **Convergence with a corrupt winner:** loser's loop top sees digest
  mismatch → removes → re-stages under a unique tmp → renames. Two threads
  can both re-stage; exactly one rename can win per absent-target window
  (atomic, refuses-existing); the loser re-enters the top, finds the
  winner's object healthy (staged bytes were digest-verified + fsynced
  before its rename) and breaks. No livelock without an external re-tearer
  (pre-existing R20-02 shape). Accounting: only the successful renamer adds
  `QFileInfo(objectPath).size()`; the torn branch subtracts only after a
  successful `remove` (:134-139) — no double-count on the new path.
- **`sPutTempCounter`** is process-wide (static function-local) — shared
  across pool instances, which is exactly what makes `pid + counter` unique
  per call within a process; two processes differ by pid. A pid-recycled
  stale tmp with the same name is handled by the `QFile::remove(tmp)` at
  :152. Sufficient.
- Pre-existing narrow TOCTOU (loser removes a just-published healthy object
  between its digest check and `remove`) is unchanged from master's R20-02
  loop and self-heals via re-stage of identical content — noted, not new.

### B.7 — #1702: scoping clean, sampleId valid, [[nodiscard]] safe

`promotePair`'s bare block `{ auto linked = ...; }` (:530-542) shadows
nothing (no outer `linked`); `sample` is in scope from :506 and its
`addSamples` succeeded (checked :525) before `sampleId()` is formatted into
the diagnostic; the final return uses the same id. `promoteTemporal`
(:630-644) symmetric. `promoteClassification`/`promoteSegmentation` declare
`linked` inside their `if (!samples.isEmpty())` scopes (:269, :384). All
four callers of `addSourceAssetsToDraft` now check the Result (grep: exactly
4 src sites); the new `[[nodiscard]]` (dataset_store.h:100) cannot break any
other caller — there are none. No production consumer of the four promotion
functions exists outside sample_promotion itself, so the new
partial-commit-then-fail contract (samples stay, failure reported) cannot
trigger a blind retry-duplication path in-tree. Consistent with the
neighboring annotation-loop doctrine.

### B.8 — #1703: four sites sound; no skipped state; no code-string consumers

All four sites (dataset_store.cpp:730, :817, :914, :1043) add the same
`else if (stmt.stepFailed()) → dataset.store_query_failed` branch after
`if (stmt.stepRow())`. `StoreStmt::stepFailed()`
(dataset_store_impl.h:52-55) is true only for genuine error rc (not
ROW/DONE/OK) — the error-vs-absence distinction is real. In
`commitVersionImpl` the early return sits on the *first* lookup; master in
the same failure returned `dataset.not_found` at :922-925 — also before the
`stagedCheck` block — so no state that master reached is skipped; only the
diagnostic code/message change. `dataset.store_query_failed` is the
established convention (dataset_store_facets.cpp, dataset_store_samples.cpp
use it throughout). No src/cli or src/agent code dispatches on either code
string (grep: zero matches; the nearest consumer,
src/cli/cli_dataset_commands.cpp:385, treats any failure uniformly) — the
retry-vs-identity-missing confusion the issue describes cannot be triggered
by an in-tree caller mis-handling the new code. `validateWindow`
(raster_reader.cpp:215-226): degenerate-size check first rejects
width/height ≤ 0, then int64 sums — the {1, INT_MAX} wrap is closed,
in-bounds and out-of-bounds verdicts unchanged (2094-assertion
test_io_raster_contract green per R1; not re-run by R2).

### B.9 — Whitelist: PASS

`git diff --name-only 911e0b17..5fc544a6c6` (incl. the mid-review commit):
`.planning/r22-data/*` (9 docs), `src/data/artifact_object_pool.cpp`,
`src/dataset/{dataset_store.cpp,dataset_store.h,sample_promotion.cpp}`,
`src/geospatial/raster/{raster_reader.cpp,raster_reader.h}`,
`src/geospatial/remote/{offline_gate.cpp,offline_gate.h,range_cache.cpp,range_cache_disk.cpp,range_cache_disk.h}`,
`src/geospatial/util/atomic_fs.cpp`, `tests/{CMakeLists.txt,test_io_range_cache.cpp,test_r22_data_security_consistency.cpp}`.
All inside OWNERSHIP.md's declared write domain. One footnote (R2-5): the
`[issue1228]` case in test_io_range_cache.cpp *modifies* an existing test's
assertions (7-arg call + expected `bs=` suffix) rather than only appending —
forced by the signature change, semantically the same assertion; acceptable.

---

## C. Test honesty (per test)

| Test | Fails on master? | Flake risk |
|---|---|---|
| r22 #1679 authority parsing | YES — master's first-@/first-/ order judges `?@` shapes public (independently re-derived from master blob by R2) | None (pure parsing, IP literals only, no DNS) |
| r22 resolveForPinning literals | YES — master returns NoResolutionNeeded under policy ON | None (policy guard restores global; literals only, no DNS; Catch2 runs cases serially) |
| r22 #1701 marker failure | YES — master ignores rename result → no throw → REQUIRE fails | None (all local fs; EISDIR injection portable) |
| r22 #1701 stale .bak | YES — master's cleanup renames the stale ANCIENT .bak over CURRENT (re-traced: cleanup's `fileExists(mainBackup)` sees the stale file) | None |
| r22 #1701 orphan .bak resurrect (new, 5fc544a6c6) | **NO — vacuous** (R2-2): master's success path also ends with target=NEW-MAIN, .bak removed, no journal — identical end state | None |
| r22 #1701 journal unwritable | NO — exercises master's pre-existing `!out` open-failure check (disclosed in-test); the new `out.good()` path untested (R2-7) | None |
| r22 #1703 validateWindow | YES — int wrap (in practice) passes validation for {1, INT_MAX} | None (UB on master is theoretical; observed two's-complement wrap is the pinned behavior) |
| r22 #1680 pool race | WEAK — probabilistic RED (needs actual interleaving in ≥1 of 8 rounds; gate + 20 ms makes it near-certain, not certain — RED_GREEN's "按构造会失败" overstates) | Post-fix deterministic (10/10 verified); no hang (polling gate) |
| r22 #1702 promotion link | YES — master ignores the Result → success → REQUIRE(!broken) fails; healthy-path block guards against fixture false-RED | None (direct SQLite corruption is deterministic) |
| range-cache #1700 store-level | YES in the geometry-blind-basis sense — master's basis string identical for both geometries → same file → CHECK_FALSE(read) fails; literal master won't compile the 7-arg call (R2-4) | None |
| range-cache #1700 integration (3 passes) | NO — vacuous in-process (uninstall() wipes disk between passes); honestly disclosed in-test and in corrected RED_GREEN.md | None (loopback fixture; offline guard present) |
| range-cache `[issue1228]` basis format | YES (assertions reference `bs=` suffix absent on master; won't compile on literal master) | None |

---

## Findings register

| # | Sev | Location | Finding | Blocks PR? |
|---|---|---|---|---|
| R2-1 | P1 (historical, **fixed by 5fc544a6c6**) | atomic_fs.cpp (f22ec241bc sweep) | The R1-fix round's unconditional `.bak` sweep introduced permanent data loss: with target ABSENT and `.bak` the only copy (backupInPlace move-aside crash + unreadable journal), the sweep ran *before* writeJournalFile; a subsequent journal-write or publish failure destroyed the only surviving bytes — a state master could not reach. R1 reviewed this sweep as "sound" and missed the target-absent residue shape. Resurrect-or-sweep fix verified correct by R2 (ordering vs recover/cleanup/had_target bookkeeping all check out). Recorded so the mid-round regression is not lost from history. | NO at reviewed HEAD (would have been YES at f22ec241bc) |
| R2-2 | P2 | tests/test_r22_data_security_consistency.cpp:260-282 | The new "orphaned .bak is resurrected" test is vacuous: on master the publish succeeds and its success path also removes the orphan `.bak` — end state identical to the fixed code, all four CHECKs pass on master. The decisive observable (publish *fails* after resurrect → target still holds OLD bytes) is untested. Suggested: block the journal path (directory) after the orphan setup and assert `target == "ONLY-COPY"` after the throw — that fails on master (target absent) and passes fixed. | NO (test-quality) |
| R2-3 | P3 | range_cache.cpp:895-902 | `clear()` on a geometry change deletes ALL `.blk` files — including blocks whose basis already matches the NEW geometry (prior same-blockSize run in the same directory, or a concurrent process sharing the directory with the new geometry). Correctness unaffected (miss→refetch), but the comment's "pure dead bytes" rationale overstates: some destroyed bytes are live and reusable. Same call also re-seeds unreachable files via the in-flight putRunToDisk window (R1 F3, unchanged). | NO |
| R2-4 | P3 | tests/test_io_range_cache.cpp:940-948 + RED_GREEN.md | Store-level #1700 oracle cannot run against literal master (compile error on the 7-arg call); its RED holds only against the geometry-blind-basis variant. The corrected RED_GREEN.md says this accurately; keep the basis-format assertions as the compile-independent master proof. | NO |
| R2-5 | P3 | tests/test_io_range_cache.cpp `[issue1228]` case | Edit to an existing test's assertions (OWNERSHIP allows "appends" to existing test files). Forced by the signature change; same assertion semantics. Note only. | NO |
| R2-6 | P3 | tests/...cpp:333-380 + RED_GREEN.md #1680 | Race test remains probabilistic-RED on master (start gate reduces but cannot eliminate serialization under load); RED_GREEN's "按构造会失败 / by construction" claim overstates. Post-fix determinism verified 10/10. | NO |
| R2-7 | P3 | atomic_fs.cpp:398-405 | The new `out.good()` ENOSPC-mid-write check has no test (acknowledged no deterministic injection point; the open-failure contract is pinned instead). Trivially correct by inspection (throw before any target is touched). Carried over from R1 F5. | NO |
| R2-8 | P4 | range_cache.cpp:777-785 | Oversized-block refusal happens *after* readBlock already counted a `hits++` and refreshed mtime — trivial stats quirk on a hand-planted-file path; `data.empty()` guard is unreachable (parseBlock rejects dataSize==0). | NO |

R1 claims attacked and independently confirmed: authority-parse curl parity
(§1, incl. the `\@` and percent-decode divergences — all fail closed);
#1700 wiring and lock ordering (§2); #1701 marker/throw/recover flow (§3);
#1680 convergence and accounting (§4 — plus the missing empirical
`QFile::rename` verification, now supplied by R2); #1702 caller closure
(§5); #1703 semantics and code-string non-dispatch (§6 — R1's
src/cli/cli_dataset_commands.cpp:385 consumer verified to exist and to treat
failure uniformly).

---

## Verdict: **APPROVE**

All six fixes (#1679, #1700, #1701, #1680, #1702, #1703) are correct at the
reviewed HEAD `5fc544a6c6`, including the mid-review hardening that closed
the one genuine regression the R1-fix round had introduced (R2-1). The lock
restructuring cannot deadlock; the SSRF parser matches or exceeds the
connector's strictness in every divergence I could construct; the pool race
converges; the promotion/provenance and window/error-code contracts hold.
Tests are honest about their own limits (the two vacuous cases are disclosed
in-test), with one undetected vacuous newcomer (R2-2) and an untested
`out.good()` path (R2-7) as the only follow-ups worth doing. Whitelist clean.
Recommended before merge (non-blocking): strengthen the orphan-.bak test per
R2-2; commit the pending RED_GREEN.md correction (it is accurate).
