# R33-T06 — REVIEW R1

Independent reviewer: a separate agent with no part in the implementation, given
only the base/head SHAs, the four claimed contracts and the instruction to read
`git diff 5ff1540..HEAD` plus the real callers. It did not build (a configure was
running); it ran `git`, the i18n selftest/gate and read whatever it wanted.

## Verdicts as returned

| Contract | Verdict |
|---|---|
| 1 Preview generation identity | **CONCERN** — cache path correct, but `identityChanged` had no consumer, so the *display* could keep showing generation A |
| 2 Non-finite display rule | **SOUND** |
| 3 Bounded pending work | **SOUND** |
| 4 i18n classifier precision | **SOUND** |

It also confirmed: no file outside the track's ownership was touched; the LRU
bookkeeping (`m_cache`/`m_lru`/`m_cacheTotalBytes`) stayed consistent; `Result`
gained its field at the end so no aggregate initialisation breaks; the new tests
assert behaviour over real GDAL fixtures, and the A→B barrier does not rely on
pool FIFO luck (one worker is serial by construction).

## Findings and disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| 1 | P2 | `Result::identityChanged` was write-only: `data_manager_panel.cpp` painted any Ready image, so a file rewritten mid-render was still displayed as the current one. | **FIXED.** The pane now re-reads exactly once when the service reports `identityChanged` (`beginAssetPreview(..., retried)` bounds it), so it shows the current generation instead of a stale one. A caption was tried first and reverted: it added a `tr()` source, which made `check_translation_freshness.py` gain a 6th FAIL while the TS is #2134-exclusive — the freshness gate is back to its inherited 5. File-watching/loop handling is H-08. |
| 2 | P3 | `pendingRequests()` is a registry count, not a count of running renders; receiver-less requests have no supersede bound. | **FIXED (doc).** Header now states both limits explicitly, including that `nullptr` callers must bound their own submissions. |
| 3 | P3 | `(v - min) / (max - min)` overflows for legitimate `{-1e308, +1e308}` spans → every ratio NaN → whole preview black. | **FIXED.** The direct form stays in charge while `max - min` is representable; beyond that the half-range form (`v*0.5 - min*0.5` over `max*0.5 - min*0.5`) keeps the denominator finite. R2 then showed the half-range *alone* underflows a subnormal-only span to a zero denominator, hence the two-path form plus a denormal fixture. |
| 4 | P3 | Identity is size+mtime, so a same-size rewrite with a restored mtime still collides — the header's absolute "never … under generation B's identity" was false. | **FIXED (doc).** Claim narrowed to "never answered *from the cache*", with the size+mtime limitation stated next to it. Content hashing is refused by design. |
| 5 | P3 | The `missing` branch of `cacheKey()` dropped the target size. | **FIXED.** Missing keys now carry width/height like every other key. |
| 6 | P3 | Switching to `bandSentinelMatches` changes Float32 non-exact sentinels (e.g. `0.1`) and no test covered it. | **FIXED (second attempt).** The first attempt used a GTiff fixture; R2 showed GDAL already re-quantises `GDAL_NODATA` to float on open, so the old `==` matched there too and the test proved nothing. The case now pins the predicate directly (`geo::bandSentinelMatches(Float32 band, (double)0.1f)` true while `(double)0.1f != 0.1`), which is the actual seam. |
| 7 | P3 | The 3-band RGB path had no non-finite test. | **FIXED.** New fixture: +Inf in the green band blackens that pixel and leaves band 2's range anchored at its largest finite sample. |
| 8 | P3 | The template exemption used `[A-Za-z\u4e00-\u9fff]`, so `"Всего %1"` was exempt (non-Latin, non-Han copy). | **FIXED.** Residue test is now `[^\W\d_]` (a letter in any script) with `re.UNICODE`; `"%s: %d"` is exempt too, for consistency with `.arg()` templates. Selftest gained the Cyrillic case and a printf-spelling assertion. |
| 9 | P3 | `REQUIRE( lastSize.isValid() )` asserted the test's own local; the cancel case name over-claimed ordering. | **FIXED.** The burst case now captures the delivered `Result` and asserts `requestedSize == lastSize`; the cancel case is named for what it proves ("neither delivers nor caches"). |
| 10 | P3 | `docs/ui-architecture.md` still described the old stretch/cancel semantics. | **FIXED.** Two sentences updated (finite-only stretch, abort-at-request, `identityChanged`, `pendingRequests`). |
| 11 | P3 | `PLAN.md` pointed at `ACCEPTANCE.md` / `REVIEW_R1..R2` before they existed, and "fails at base" was not literally true (the test uses new API). | **FIXED.** Both files exist now; the WP02 row says the comparable fixture reproduced the pollution on base instead of claiming the new test compiles there. |
| 12 | P3 | `<QSet>` include left behind; the abort-flag comment implied a migration between requests that does not exist; `pendingRequests()` had no thread-affinity note. | **FIXED.** Include removed, comment rewritten, thread affinity documented. |

## Claims the reviewer showed were overstated (all corrected)

1. "never answered … under generation B's identity" → true only for the cache;
   the panel answer is now captioned rather than silently stale (finding 1).
2. "a burst cannot leave a thousand renders queued" → the pool *queue* still
   receives one no-op lambda per click; what is bounded is real pixel work
   (`maxThreadCount`, plus the one render already inside GDAL). Header and
   commit message wording already say "abort the queued worker", not "not
   queued"; `pendingRequests()` doc now spells the distinction out.
3. "m_canceled became dead state" → correct, but the reviewer is right that
   removing it moved the memory bound onto the registry, which does not bound
   `receiver == nullptr` callers. Documented (finding 2).

## Not changed (with reason)

* **Content fingerprint for the preview cache** — refused by the brief
  ("不为预览全文件 hash 巨型影像"); the size+mtime limitation is now documented
  instead of hidden.
* **A worker-side "currently rendering" counter** — nothing consumes it yet;
  adding it would be speculative API (YAGNI). Recorded in HANDOFF H-08.
