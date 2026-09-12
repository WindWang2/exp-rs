# BASELINE — Scientific Algorithms 9.0

Audited 2026-09-11 against `origin/master` = `132da5e998eca004d43285d1f71565f973030f5f`
(merge of PR #847, geospatial-data-fabric-8). Worktree:
`/home/kevin/projects/rs-studio/exp-rs-scientific-algorithms-9`, branch
`feat/scientific-algorithms-9`.

## 1. Repository state at audit time

- **Open PRs:** none.
- **Open issues:** #848–#882 (a 2026-09-11 defect wave, all `ready-for-agent`).
- **Local unpushed state (lead, not fact):** the shared checkout `main` sits on
  local `master` = `origin/master` + `8f6293bceb` (unpushed "resolve P0 defects
  #848–#852") plus uncommitted working-tree edits apparently targeting
  #853–#882. None of that is in this baseline; this track re-derives its own
  fixes from the issues on top of `origin/master` and keeps `main` untouched.
- **Remote branches:** `origin/feat/*-5/6/7/8` are merged historical residue
  (verified against merge history of PRs #761–#847). `itk-upstream/*` is the
  vendored ITK mirror. A parallel `feat/execution-concurrency-lifecycle-9`
  worktree exists (`exp-rs-exec-concurrency-9`); no shared-file coordination
  needed so far (it owns scheduler/concurrency files, this track owns
  scientific algorithm semantics).
- **Recent merged PRs:** #846 scientific-processing-8, #847
  geospatial-data-fabric-8 (both inspected; see §3 and OVERLAP_MAP.md).

## 2. Scientific issue re-verification (latest master)

Re-verified by reading the code on `origin/master` (not by trusting the issue
line numbers):

| Issue | Verdict | Evidence on this baseline |
|---|---|---|
| #848 fillDepressions NoData border | **still-valid** | `terrain_flow.cpp:48-63` — queue seeded only on the rectangular perimeter; NoData borders ⇒ empty queue ⇒ 0 fills, returns true |
| #853 D8 float→int UB | **still-valid** | `terrain_flow.cpp:143` (`static_cast<int>(d)` before range check; finite-but-huge values are UB) and `terrain_flow.cpp:285` (isfinite guard but no range guard) |
| #854 SAR flatten mask NoData | **still-valid** | `rs_sar_terrain_flatten_operator.cpp:233` — dataset-wide NaN NoData over a 2-band (Float32 gamma0 + Byte validity) output; kernel writes Byte 255 ⇒ 255 ≠ NaN on read-back |
| #855 anisotropic Horn gradient | **still-valid** | `sar_terrain.cpp:152` averages `cellMeters = 0.5*(cellX+cellY)`, both Horn denominators use it. `terrain_analysis.cpp` is already anisotropic-correct (cellSizeX/Y) — defect is SAR-specific |
| #856 spectral negative sentinel | **still-valid** | `rs_spectral_index_operator.cpp:302-303` `std::abs(v)` promotes −9999 → +9999 ⇒ DnScale; lines 342-377 `isScaledDataset` is write-only (dead) |
| #873 domainFromDeclaredScale +Inf | **still-valid** | `scientific_contracts.cpp:27` `declaredScale > 0.0` accepts +Inf ⇒ divisor +Inf ⇒ invScale 0 |

Not re-fixed here (other tracks' ownership, being remediated in the shared
checkout by a parallel sweep): #851/#852/#857–#866/#868/#869–#871/#874–#882.
#874 (RasterReader Float32 sentinel equality) and #875 (SplitEngine folds)
are recorded in ISSUE_TRIAGE.md as `out-of-scope`/`adjacent` with rationale.

## 3. What 8.0 (and predecessors) already delivered — do NOT re-implement

From `.planning/scientific-processing-8/{BASELINE,CAPABILITY_MATRIX,FINAL_REPORT}.md`,
all merged in PRs #829/#846:

- SAR: orbit interpolation + zero-Doppler machinery (`sar_orbit`), backward
  geolocation, forward range-Doppler geocoding + Ulander gamma0 RTC
  (`rs:sar_geocode`), temporal stats (`rs:sar_temporal_stats`), calibration /
  speckle / texture / dualpol / ratio / change kernels + operators, linear/dB
  domain contract.
- Temporal family: MK/Sen (tie-corrected), seasonal MK, harmonic, phenology,
  breakpoints, decomposition, anomaly, gap-fill, composite — one time-axis
  contract (docs/processing/temporal.md).
- Spectral: broad index family + formula drift guard
  (`test_spectral_formula_drift.cpp`), continuum removal, SAM/SID, PCA/MNF,
  unmixing, endmember extraction.
- Raster/vector: `rs:rasterize`, `rs:zonal_stats` (windowed, Welford).
- Terrain/hydrology: fill/D8/accumulation/watershed with documented contracts
  (NoData-as-barrier, deterministic ties), anisotropic-capable
  `terrain_analysis` kernels.
- Classification: SVM/RF/MLP/kNN/NB/ISODATA pipeline, deterministic seeds,
  accuracy assessment.
- Contract layer: `scientific_contracts.h` (numeric domain once per raster),
  `docs/processing/*.md` vocabulary.

## 4. Real gaps for 9.0 (this track)

1. **M0:** the six verified defects above, with old-code-fails/new-code-passes
   regression tests.
2. **M1:** the semantics contract is documented but *not mechanically
   enforced* per kernel — SAR terrain anisotropy slipped through; sentinel
   heuristics (`v != -9999.0f`) survived in operator code. Deliver a shared
   numeric-semantics seam in `src/processing/contracts` + a drift test that
   fails when a kernel/operator reinvents scale/sentinel logic.
3. **M2:** SAR known-answer coverage is thin on **anisotropic grids**, and the
   flatten operator's per-band NoData; local-incidence closed-form checks vs
   analytic slopes exist for isotropic only.
4. **M4:** spectral additive constants under DN scale (#801 family) have no
   degenerate-denominator / sentinel mix known-answer cases.
5. **M6:** terrain family NoData-border fill (#848) + safe D8 casting (#853).
6. **M9:** extend the corpus so each M0/M2/M4/M6 fix has an independent
   reference (closed-form or analytic raster), not a self-comparison.

## 5. Host/toolchain evidence

clang 22.1.8 (`/usr/sbin/clang++`), ccache, 16 cores / 62 GiB RAM; Debug
builds, `-j2..-j4` target-scoped builds per the resource policy. Same
toolchain family as the 8.0 evidence (clang 22.1.8, Debug).
