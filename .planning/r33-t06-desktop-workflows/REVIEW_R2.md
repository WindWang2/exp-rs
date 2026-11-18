# R33-T06 — REVIEW R2

Second independent reviewer, again with no part in the implementation, given the
base/head SHAs and the full R1 finding list. It was told not to build (the host
build lock is held by this track's own clean build) and verified by reading the
diff, running the i18n gate/selftest and constructing inputs by hand.

## Result

`FINAL: No` — three blocking items at R1-close, all cheap. Everything else in
the twelve R1 findings verified as fixed except two, which were fixed wrongly.

| R1 # | R2 verdict | Note |
|---|---|---|
| 1 | CONFIRMED-FIXED | The re-read is bounded to one retry, the superseded token is already erased so the nested `requestPreview` cannot be dropped, and the "Loading preview…" state survives. **No automated test** (see below). |
| 2 | CONFIRMED-FIXED (doc) | — |
| 3 | CONFIRMED-FIXED (overflow) | Fuzzed over 300k log-uniform spans: monotonic, in range, no NaN. **But** the fix regressed a subnormal-only span (new #2). |
| 4 | CONFIRMED-FIXED (doc) | — |
| 5 | CONFIRMED-FIXED | — |
| 6 | **NOT-FIXED (first attempt)** | GTiff re-quantises `GDAL_NODATA` to float on open, so the fixture passed on the *old* comparison too. Replaced with a direct predicate test. |
| 7 | CONFIRMED-FIXED | 3-band +Inf genuinely fails on the old code. |
| 8 | CONFIRMED-FIXED | Gate: `hard_en 1→0`, every other counter byte-identical. Reviewer also measured that the rule removes 20 `hard_en` findings across 17 files when run over **all** `src/**/*.cpp` (16 of them outside the audited list) — see the note below. |
| 9 | CONFIRMED-FIXED | — |
| 10 | CONFIRMED-FIXED | — |
| 11 | **NOT-FIXED at review time** | `ACCEPTANCE.md` did not exist yet; `PLAN.md` and `PR_BODY.md` referenced it. Written before this delivery (see ACCEPTANCE.md). |
| 12 | CONFIRMED-FIXED | — |

## New findings and disposition

| # | Sev | Finding | Disposition |
|---|---|---|---|
| N1 | P3 | The Float32-sentinel fixture does not discriminate the narrowing (see R1 #6). | **FIXED.** The case now asserts `sicnu::geo::bandSentinelMatches(Float32 band sentinel 0.1, (double)0.1f)` is true while `(double)0.1f != 0.1`, plus Float64 exactness and a NaN sentinel. This is the public seam the service actually calls. |
| N2 | P3 | The half-range form alone blanks a subnormal-only span (`{0, denorm_min}` → both halves round to 0 → 0/0). | **FIXED.** Two-path `scaleToByte()`: the direct form while `max - min` is finite and positive, half-range only as the overflow fallback, flat gray if even that is unresolvable. New denormal fixture pins min→0 / max→255. |
| N3 | P3 | `.gitignore` is written although `PATH_OWNERSHIP.md` listed it as #2134-exclusive; a textual conflict is guaranteed at EOF. | **DISPUTED with evidence.** The track brief explicitly permits it ("`.gitignore` 只允许自身planning记录的必要追加，不改原PR块"), the repo's own convention is one exception block per track (~40 existing), and the append touches no existing rule. `PATH_OWNERSHIP.md` now records the sanction and the expected pure-addition conflict instead of hiding the discrepancy. |
| N4 | P3 | `DECISIONS.md` D-02/D-09 contradicted the implementation after the R1 fixes. | **FIXED.** D-02 records the two-layer answer (service reports, consumer re-reads once); D-09 says "no letter in any script". |
| N5 | P3 | `Result::identityChanged` over-specified ("the generation current when the request was made"). | **FIXED.** Reworded to "a generation other than the one the key describes" — a replace landing before the worker opens the file yields the later generation. |

## Reviewer's own caveat, recorded

> `beginAssetPreview`'s retry has no test at all, and I could not build or run
> anything (build lock held), so even its compile status is unverified here.

Answered in ACCEPTANCE.md: the panel TU is compiled (whole-target build) and
behaviourally exercised only at the "compiles + syntax" level; the retry path is
explicitly handed over as H-08 with `test_data_manager_panel` named as the
missing coverage.

## Where the reviewer was wrong, and it matters

* It read `PATH_OWNERSHIP.md` as authoritative and concluded `.gitignore` must
  not be touched. The *brief* sanctions the append; the ledger was the thing out
  of date, and it is now corrected (N3).
* It suggested the "one literal in the whole repository" claim in R1 was false
  (20 findings over all of `src/**`). That is true and worth having on record:
  the exemption is a general rule, not a one-off carve-out for the pager row.
  The canonical gate only scans the frozen 92-file list, so the audited number
  still moves by exactly one.
