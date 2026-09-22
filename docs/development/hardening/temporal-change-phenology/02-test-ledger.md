# Test Ledger — temporal-change-phenology (19/20)

Baseline: master `a9dc33fa7`, branch `hardening/temporal-change-phenology`.
Build: Debug, `-DENABLE_TESTS=ON` + `CMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`,
`-j2` cap. Tests need `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib`
(pwb-sdks libodbc; system unixodbc absent).

## Pre-existing RED on master (not introduced by this branch)

Verified by running the untouched suites on a tree whose only deltas were
appended tests/docs (`git status` = 3 test files + docs): **master HEAD is
red** in three suites, two root causes, both inside this track's scope:

### RC1 — phenologyThreshold edge semantics regressed by #1229

`temporal_fit.cpp phenologyThreshold`: #1229 replaced the quantized
"first/last sample ≥ threshold" SOS/EOS with strict interpolated crossings
(`v0 < level && v1 >= level`). Windows that OPEN at/above the threshold
(season already running) or flat seasons have no rising bracket → SOS/EOS =
NaN → invalid metrics. RED tests (all pre-existing, all asserting the old
anchored contract):

- `test_temporal_irregular` "window opens mid-ramp" / "flat season" (2 cases)
- `test_temporal_change` "complementSeasonWindow + phenologyCyclesPerYear"
  (window {60,200} opens at 51% amplitude)
- `test_temporal_phenology_multi` "long-gap" branch: refusal
  `threshold_crossing_failed` now reachable, exposing that `out.pos` was
  assigned BEFORE validity (fabricated peak doy on refused cycles;
  test asserts `pos < 0`).

Fix F6: edge anchoring (SOS = first in-season sample when it is already ≥
threshold and no rising bracket exists; EOS symmetric; limb metrics stay NaN)
+ `out.pos` published only when the metrics are valid.

### RC2 — #1200 day-axis trend default eats the annual signal

#1200 routed `seasonalDecompose`'s trend through `whittakerSmoothTime` (day
axis, correct) but kept the index-axis-era default λ=1e4. At the annual
frequency the day-axis penalty response is `λ·(4sin²(πh/T))²` — ≈1e-3 at
daily cadence, O(10) at the corpus's 16-day cadence (partial absorption) —
far below the stiffness the old index-axis λ implied. The trend absorbs the
seasonal sinusoid, the climatology degrades to noise, and
`phenologyMultiCycle` peak detection invents 3 cycles/year on a
single-season corpus (empirically: seasonal range 0.032 for a 0.7-amplitude
input). RED tests: 7 checks in
`test_temporal_phenology_multi` (cyclesPerYearMax 3 vs 1/2, wrapped-window
validity 0, December harvest-year attribution lost).

Fix F7: default λ 1e4 → 1e8 (penalty response at annual ≈ 1e4 ≫ 1; cutoff
period ≈ 2π·λ^¼·h ≥ 1.7 years at any cadence) in `PhenologyMultiOptions`,
`seasonalDecompose`'s fallback, and the `rs:temporal_decompose` operator
default. The existing λ=1e8 day-scale decompose known-answer test
(test_temporal_fit.cpp:408) pins the scale.

## New oracles (RED → GREEN on this branch)

| # | Oracle | RED on master (observed) | Fix |
|---|---|---|---|
| O1 | `test_bfast_harmonic_breaks` "Breakpoint index maps back … under NaN gaps" | `t[bp.index] ≠ bp.tDays` (index 39 = sub-vector position, not 46 = series position) | F1 `originSorted[best.split]` |
| O2 | `test_phenology_extraction` "Multi-cycle extraction keeps day-366 …" | peakVal 0.4 / pos 380 (day-366 peak dropped) | F2 window `1..366` |
| O3 | `test_temporal_algorithms` "temporal_decompose refuses duplicate scene times …" | run succeeds, silent all-NaN trend + fabricated all-zero seasonal | F3 typed `InvalidInputData` before output |
| O4 | `test_temporal_algorithms` "temporal smooth and phenology honor gap-fill provenance" | coverage only (paths exist, untested) | — |
| O5 | `test_temporal_algorithms` "gap-fill provenance_output wires into downstream …" | (a) single multi-band artifact → count-mismatch throw; (b) same path ×N → silent band-1-for-all (n=6 vs truth 3) | F5 band-mapped artifact form + prov_<date> mismatch refusal |
| O6 | RC1/RC2 suites listed above (pre-existing RED) | 10 failing checks on master | F6/F7 |

## Already-fixed surfaces (regression only, per campaign seed)

- 365.25 doyOf + day-366 bucket (#1229) — `test_temporal_irregular.cpp:494` leap-day calendar axis, `:529` year-boundary season,
  `test_temporal_fit.cpp:584`; extended by O2.
- MOSUM σ√h, true MAD, interpolated SOS/EOS (#1229) — existing suites; SOS/EOS
  edge contract restored by F6 without touching the interpolated midpoint
  semantics #1229 pinned.
- Gap-fill provenance consumed downstream (#1200/#1167) — sen_trend/trend E2E
  at `test_temporal_algorithms.cpp:709`; smooth/phenology covered by O4;
  artifact wiring by O5.

## Independent adversarial review (round 1)

Verdict: **READY**, no P0. Findings and disposition:

- P1 artifact branch mapped bands positionally without date validation →
  FIXED (band `prov_<date>` names must match the consuming collection's
  scene tags; refusal names the first mismatching band) + oracle
  `[p1-review]` (was RED: mismatched artifact accepted positionally).
- P2 edge-anchored windows can publish `sos == pos` / `eos == pos` →
  documented in `temporal_fit.h` (time-ordered, not strictly interior, by
  the legacy quantized contract the tests pin).
- P3.1 decompose duplicate-time error now names the scene DATES → fixed.
- P3.2 `temporal_spatial_tools` window aligned to `1..366` (unreachable
  day-366 there, convention alignment only) → fixed.
- P3.3 doc numbering/formula scope → fixed (this file + 01-recon.md).
- P3.4 decompose value-level E2E at the new default λ → added
  (`[p3-review]`: trend ≈ base, seasonal carries ±5 annual swing).

## Closure

- Final verification: 21 suites × 2 consecutive passes, all green
  (temporal_fit/irregular/change/core/calendar/phenology_extraction/
  bfast_harmonic_breaks/phenology_multi/uncertainty/algorithms/selection/
  operators_ti11/regions/operators_10/workspace/agent_tools/d16_trend/
  d16_tools/d16_phenology_e2e/spatiotemporal_contracts/capability_drift).
- Commits 7e4b01ed8..e7e15f16a on hardening/temporal-change-phenology
  (rebased onto master a9dc33fa7 — master did not move during the slice).
- PR #1244 created 2026-09-23. NOT merged; online CI not awaited.
