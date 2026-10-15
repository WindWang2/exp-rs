# R14-E Independent Review Record

## Round 1 (fresh reviewer agent; full-diff adversarial read)

Verdict: **FAIL** — findings below; all P0/P1 fixed same session (see resolutions).
NOTE: findings 1 (dock P0) pertained partly to the studio-dock file, which was later DROPPED
from this branch entirely at push time (WP08 dedupe against open PR #1510). The P0 fix was
verified before the drop; the file no longer ships in this PR.

| # | Sev | Finding | Resolution |
|---|-----|---------|------------|
| 1 | P0 | experiment_studio_dock.cpp demo path: const-shadowed `leftId/rightId` assigned in else branch — ill-formed C++, build-breaking | FIXED: shadowing removed; defaults kept in outer scope (`selectedLeftId/selectedRightId` locals) |
| 2 | P1 | pair_crs unresolved-emission breaks golden pin `test_preflight_golden.cpp` (`SPF_CRS_UNKNOWN == 0`) | FIXED: conscious golden-matrix update (contract change documented in-test; CRS/RESOLUTION/QUALITY == 1, TEMPORAL == 0 — the temporal rule keeps its deliberate no-policy pass) |
| 3 | P1 | root-level symlinks refused in BOTH enumeration loops → duplicate skip rows (2nd unattributed); track's own new test would fail | FIXED: flat pass now owns only plain visible files (`isSymLink()/!isFile()/isHidden()` guard); links/specials refused exactly once, with attribution, in the directory pass |
| 4 | P2 | hidden-entry invisibility relied on unverified QDir type-less-listing semantics | FIXED: explicit `isHidden()` guards in all three loops; old invisibility no longer depends on filter side effects |
| 5 | P3 | pair rules still dropped unresolved extra slots when ≥2 rasters resolved | FIXED: post-loop `emitUnresolvedSlotUnknowns` in pair_crs + pair_resolution_ratio (mismatch findings still dominate via outcomeFor) |
| 6 | P3 | readBounded growth-window truncation could digest a partial prefix | FIXED: post-read size-consistency check → unreadable |
| 7 | P3 | broker cache key read revision+digest as two acquisitions (torn pair) | FIXED: `RecipeRouter::revisionAndDigest()` single-lock snapshot used by broker |
| 8 | P3 | OWNERSHIP.md omitted test_science_context_broker.cpp | FIXED (doc) |
| 9a | P3 | 10s wall-clock bound in large-matrix test is generous-but-finite | KEPT: 1000× headroom over measured cost; structural oracles are the real pin; timing recorded via CAPTURE |
| 9b | P3 | router concurrency test is a smoke oracle | KEPT + declared: race detection proper belongs to the TSan lane (comment in-test) |
| 9c | P3 | NFC normalization invalidates resume for NFD-keyed checkpoints (one-time re-grade) | DOCUMENTED in LEDGER residual notes (fail-safe direction) |
| 9d | P3 | quality_mask/temporal now emit unknowns for unresolved slots broadly | INTENDED contract change, now pinned in the golden matrix (finding 2) |

Reviewer also verified clean (no action): session envelope invariant holds for all five
self-produced recorder paths; router locking non-recursive, registry I/O outside lock,
one-way broker→router ordering; sidecar writer cannot emit non-finite; LO grammar matches
all data; NFC covers every studentId consumer seam; dock model lifetime/lookup/objectName;
scope: no out-of-ownership files except the two test files now listed.

## Round 2

(to be signed after GREEN×2 runs on the final tree)

## Round 2 (fresh reviewer agent; verdict on the committed diff)

**VERDICT: PASS — P0 = 0, P1 = 0, P2 = 0.** All eight round-1 resolutions verified present
and correct against full-function reads; new-bug hunt over the round-1 fixes themselves
(allEntries entry-class walk, router move semantics, pair-rule severity interaction,
readBounded equality reasoning, golden pin vs actual emissions) found no defects.
Scope confirmed: 22 files, exactly OWNERSHIP.md's list; #1509/#1504 territories absent.
RED/GREEN evidence audited coherent.

Round-2 P3 notes (accepted as documented residuals, no action this track):
1. A HIDDEN symlink/fifo now gets a visible refusal skip row (refusal runs before the
   hidden guard) — fail-visible direction, conservative; comment wording adjusted.
2. `out.bytes.size() < 0` in readBounded is dead defensiveness (size() never negative).
3. Router move-assignment uses this-then-other lock order; no cross-object
   move-assignment usage exists (only move-construction); scoped_lock would be
   belt-and-braces.
4. (fixed in this file) REVIEW.md wording on the golden TEMPORAL pin imprecision.
