# PLAN.md — Track 7 (R4): RS Operator Correctness Deep Audit

Working plan after Phase 0 evidence (see BASELINE.md). Reality corrections
vs the task brief are recorded here and in DECISIONS.md; the goal-loop rule
"if a WP premise is measured false, narrow it with evidence, don't do make-
work" applies.

## Ground truth corrections from the audit (evidence in NODATA_SEMANTIC_MATRIX.md)

1. **The brief's P0 claim is REFUTED for the six named statistical
   operators.** `rs:zonal_stats`, `rs:focal_stats`, `rs:temporal_summary`,
   `rs:segment_stats`, `rs:sar_temporal_stats` and the regress/segment
   inference engine (`tile_inference_engine.cpp`) all read the declared band
   sentinel and EXCLUDE sentinel/NaN pixels from their statistics (Welford or
   masked accumulation), counting them in `nodata` columns. WP-A therefore
   becomes: fixation (anti-regression) known-answer tests for these six +
   repair of the operators that DO ingest sentinels as data.
2. **Where the brief's suspected defect class actually lives** (measured):
   - `rs:pca` — mean/covariance ingest declared sentinels as valid spectra
     (rs_pca_operator.cpp → ImageEnhancement::processPcaFile, no sentinel
     handling anywhere in the path). P1.
   - `rs:spectral_derivative` — no sentinel read; declared −9999 participates
     in finite differences. P1.
   - `rs:image_enhancement` (ratio path) — declared sentinel pair yields
     ratio 1.0; no output nodata declaration. P1.
   - `rs:contrast_stretch` — output holes rewritten with input sentinel but
     never declared → undeclared −9999 holes. P1.
   - `rs:band_ratio` IHS mode — NaN holes, no nodata declaration. P2.
   - `rs:spectral_similarity` — hardcoded −9999 sentinel, declared sentinel
     ignored. P2.
   - `rs:sar_polsar_decompose` — sentinels accumulated in covariance
     windows; outputs undeclared. P1 (fix: exclusion + declaration).
   - `rs:sar_phase_filter` — NaN outputs never declared. P2 (small).
   - `rs:register_images` / `rs:resample` — warp seam has no nodata
     contract (hardcoded −9999 or none). Fix scoped to output declaration +
     sentinel pass-through where minimal; full warp-nodata contract →
     backlog (touches shared geometric seam, risk to other tracks).

## Work packages (revised by evidence)

- **WP-A** NoData exclusion fixation tests (zonal/focal/temporal_summary/
  segment_stats/sar_temporal_stats/regress-engine) with closed-form truths;
  PCA sentinel-exclusion repair (red→green).
- **WP-B** Mask semantics: apply_mask closed-form mean-shift assertions;
  qa_mask fail-closed assertions; band_ratio-IHS + contrast_stretch +
  image_enhancement output-declaration repairs (the "masked/undeclared
  output" family).
- **WP-C** Three tolerance chains: (1) radiometric DN→radiance→reflectance→
  NDVI with closed-form end-to-end; (2) change difference/CVA on linear
  ramps (closed form); (3) terrain slope=atan(2)/aspect on the analytic
  z=2x plane through terrain_analysis. Chain-end error asserted in closed
  form.
- **WP-D** Determinism digests: same-input double-run byte-identical outputs
  for ≥12 operators spanning families (spectral index, band_math, change,
  terrain, sar, fusion, zonal CSV, contrast stretch…), including a
  tile-boundary (non-multiple-of-256) grid case.
- **WP-E** Fail-fast preflight refusals: typed RSOperatorError for CRS-less
  raster, band out of range, empty vector layer, wrong-dtype mask grid,
  missing band role — across representative operators.
- **WP-F** Known-answer gap-filling: continuum removal analytic envelope,
  BRDF normalization closed form, sar_calibrate closed form, threshold
  ramp histogram, spectral resample closed form, MNDWI/NDWI aliases,
  quality_mosaic pick rule, obia_segment label contract (8–10 operators).
- **WP-G** CMake registration; matrix R4 section in
  docs/verification/KNOWN_ANSWER_MATRIX.md; double green run.

New test files (each a `sicnu_add_test` target, linking the
sicnu_processing closure):
- `tests/test_operator_nodata_semantics.cpp` (WP-A + WP-B)
- `tests/test_operator_chain_tolerance.cpp` (WP-C)
- `tests/test_operator_determinism_digest.cpp` (WP-D)
- `tests/test_operator_preflight_refusals.cpp` (WP-E)
- `tests/test_known_answer_corpus_r4.cpp` (WP-F)

## Gates (from the brief, section 6)

- `ctest -R "operator|nodata|known|determinis" -j1` green twice in a fresh
  build dir; ≥25 new cases; ≥12 digest cases; ≥3 chain assertions.
- NODATA_SEMANTIC_MATRIX.md ≥60 operator rows with disposition.
- ≥10 atomic commits, each independently compilable; ≥14 files touched, all
  in whitelist; matrix R4 section ≥10 rows 1:1 with cases.
- Ledger with per-round verification + honest token accounting; PR with
  honest scope statement. Token-budget arithmetic in the brief (280M)
  describes a multi-agent harness budget, not a per-session measurement; the
  ledger records honest per-round counts from this session's subagent usage
  metering, and the PR states the delta explicitly rather than inflating.
