# Decisions

## D001 — goal execution fallback

The registered skill listing and repository search contain no `goal-loop`; use the user's explicit `/goal` fallback. Maintain this track's own persistent ledger, real completion states, checkpoints and evidence rather than inventing flags or modifying hooks/permissions.

## D002 — execution base

Use freshly fetched `origin/master` `85ce917bea0ece4df6548a677b97a72ecde8659b`, not the original local `master` at #2063. Create exactly one T01 branch/worktree/build directory. Rebase only after refreshing upstream again.

## D003 — resources

Use the user-authorized whole-host ceiling of 4 through the existing guard, with both the canonical host lock and this track's build lock. CTest and Node behavior tests are serial. Lower to 2/1 under memory or linker pressure. Do not change guard defaults or use unlimited parallelism.

## D004 — implementation selection

Start with WP02/WP03 output/report semantics and WP04 cache reconciliation because they share one production delivery chain and have explicit mixed-field/report fixtures. Investigate WP05, WP07–WP18 in parallel read-only; implement only behavior reproduced or conclusively demonstrated by a production-call-chain oracle. Mark already-satisfied items with evidence instead of adding duplicate infrastructure.

## D005 — active PR boundary

While #2061 is open, its current business files are read-only. `tests/CMakeLists.txt` is shared only through an independent `test_r31_t01_*` block. No other track's implementation is cherry-picked or copied.

## D006 — evidence scope

Do not wait for, trigger, rerun, poll, or close online CI. Local validation and independent reviews gate the PR. Missing optional providers/platforms are ENV_LIMITED or NOT_EXECUTED, never PASS. No merge, auto-merge, release, issue closure, or modification of another PR.

## D007 — WP02 output declaration contract (implemented, verification pending)

Use existing descriptor semantics instead of path-shaped heuristics:

- `PortDescriptor.producesFile` is the explicit file/value discriminator, populated from `SicnuFileRole` and each adapter's actual output contract. A concrete `fileFormat` may refine the kind, but a Raster/Vector/Table type alone never turns a value report into a file.
- An explicit null `outputs` schema means no declared outputs and must suppress the legacy synthetic Raster fallback. An empty output object retains the historical single-`output` fallback for old operators.
- Unknown legacy algorithms keep the single `output` result convention. A registered algorithm with exactly one file port uses only that port's declared parameter/result key. Arbitrary strings, including `path`, `digest`, `driver`, `kind`, and report-shaped `output`, never become assets by inference.
- Zero file ports means no commit/cache/autoLoad. Multiple file ports retain each declared name/path in an additive `declaredOutputs` report object; the singular legacy commit/autoLoad channel is not allowed to pick one arbitrarily. Extending transactional per-port publication is a separate proven need, not something to fake with first-output-wins.
- `AlgorithmTaskInfo` gains only additive declaration metadata, with an unresolved default that preserves direct/legacy callers. Registry lookups stay on the lock-free submission warm path, never under the TaskCenter mutex.

Tradeoff: multiple declared file outputs are preserved and not falsely published through the singular channel; this slice does not claim multi-asset transactional publication. The mandated real-producer oracle is single-output and remains fully committed.

## D008 — WP11/WP12 admission boundary (design only)

Add a pixel-sink form of the existing rasterizer and make its QSet-returning API a compatibility wrapper. Training collection uses the sink: a new unique candidate is checked against `maxCandidates` BEFORE `pixelOwner.insert`, so a huge ROI cannot first materialize QSet→QVector and only then be refused. Duplicate pixels still replace both class/geometry fields atomically (last geometry wins). Cached `pixelIndices`, invalid-index handling, progress spacing, rasterize windows and typed 4103 propagation remain unchanged.

A separate working-set admission must cover all subsequent candidate/byClass/matchCount/keepRows/cap/X/y/group containers, including keptRows×bands. It must not alter the established Fisher–Yates sequence, cap-after-NoData, class/pixel row order or groupIds. Any byte-budget refusal is typed and releases partial results. Exact container constants and independent small-oracle results will be measured before claiming a memory improvement.

## D009 — WP16 digest/sample snapshot (design only)

The safest small direction is to hash full bytes and collect head/middle/tail sample blocks in ONE stream, rather than hashing once and re-opening for a sample. This removes the specific old-digest/new-sample stitching window and avoids another full read. An atomic replace then leaves at worst an internally consistent old entry that a later fresh sample rejects, never an old digest paired with a new sample. Re-stat/read failures bypass memoization. The >192 KiB unsampled-middle limitation remains disclosed, and immutable/catalog digest identity remains the strong path.

Do not claim that this alone proves immunity to an in-place same-state rewrite racing the stream. The test oracle needs controlled interleavings plus sample/full-digest consistency, unchanged fast hits and next-acquire invalidation. Implementation waits for WP02–WP05 closure and a concrete test seam.

## D010 — WP15 release identity (design only)

Keep the existing `release(framework, identity)` signature and its established raw-digest caller (`test_device_planner`). Match the full `framework|identity` component in the cache key:

- raw artifact identity releases that artifact on every device and under every provider load contract;
- an identity containing `|pkg:` is compared as package identity, not truncated at the last separator;
- an identity containing `|load:` releases only that exact load configuration;
- empty identity still refuses the match-everything hazard, and live shared_ptr holders stay valid while reload creates a new session.

The matcher must not infer identity from the final `|` segment. Tests will use two loopback load contracts, package-bearing identity, reload counts, live holders and the VRAM ledger. Public labels and internal keys remain credential-free; URL userinfo never enters a fixture assertion or log.

## D011 — WP04 content-aware reconcile (design only)

Use one optional provenance field for actual output SHA-256, computed through the existing streaming geospatial hash authority on initial commit and on migration/refresh. Do not add a second committer or cache.

- Ready + matching digest + intact provenance is the only zero-write unchanged hit: same AssetId, no revision advance, no autoLoad replay.
- An Error snapshot recovers through the existing DataManager refresh path, including identical restored bytes; no full raster recomputation is required.
- A digest mismatch refreshes structure/revision/provenance and cannot masquerade as a cache hit. A kind mismatch is a typed refusal rather than silently changing asset identity.
- A legacy asset without a digest gets one conservative migration refresh, then later unchanged hits are provable and cheap (digest comparison only, not repeated full runs). Provenance missing is reattached with a truthful warning on failure.
- Hashing happens once per publication/reconcile, not per tile. Performance evidence must include wall time/RSS for a fixed raster and disclose the extra publication read.

The existing `reconcilePublished()` path-only shortcut must not survive as a universal proof of content identity.
