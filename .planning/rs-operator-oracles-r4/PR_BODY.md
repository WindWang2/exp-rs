# Track 7 R4: RS operator correctness deep audit — NoData semantics, tolerance chains, determinism

## Scope honesty statement (read first)

The task brief budgeted a multi-agent, 6h+/280M-token harness run. This PR
was produced in a single-agent session; the token ledger records honest
metered usage (~9.4M subagent tokens at review time), **not** the brief's
280M figure — see `.goal-loop-ledger.md` and DECISIONS D8. All numeric gates
below are real and verifiable; the token-budget arithmetic gate is reported
as not-met-by-design rather than inflated.

Also honest: the brief's central P0 premise — that the six statistical
operators treat NoData as zero — was **refuted by the audit** (all six read
the declared sentinel and exclude it from their statistics). Per the brief's
own goal-loop rule, that premise was recorded as refuted (matrix rows 1–9,
row 112 correction) and the fix effort went to the 8 sentinel-as-data /
undeclared-output defects that the audit did find. The initially suspected
`rs:pca` defect was likewise refuted on direct re-verification and corrected
in the matrix.

## Baseline & overlap

- Base: `origin/master` `15e5c66b5` (unchanged since brief; re-fetched).
- Open PRs at start: #1334–#1338. **No open PR touches
  `src/operators/rs/`, `src/processing/algorithms/`, or the matrix doc.**
  `tests/CMakeLists.txt` is append-only here (tail block), expecting the
  same textual-append pattern as #1334/#1335 — rebase should be trivial.
- Worktree-isolated: all work in `exp-rs-operator-oracles-r4`, branch
  `hardening/r4-operator-oracles`, root checkout untouched.

## What changed (17 commits, 23+ files, all in the declared whitelist)

### Defect fixes (each atomic, each regression-pinned)

| commit | defect (matrix row) | fix |
|---|---|---|
| be1b0f8ee | rs:spectral_derivative — declared sentinel enters finite differences (row 48) | NaN-ize declared sentinels per band; no-sentinel path bit-identical |
| b6225cf35 | rs:spectral_similarity — hardcoded −9999, declared sentinel ignored (row 105) | resolve first declared finite sentinel on used bands, pass to kernel |
| bbf5e7ee0 | rs:image_enhancement — ratio path counts sentinels as data (row 24) | masked bandRatioTile + per-method output NoData declarations |
| c6e90edea | band_ratio IHS / contrast_stretch — undeclared holes (rows 21/23) | declare exactly what the kernels write (NaN / resolved sentinel) |
| f4a19336e | rs:sar_phase_filter — undeclared NaN outputs (row 66) | declare NaN on the phasor band |
| 9003c4279 | rs:register_images — undeclared warp voids (row 85) | declare the warp sentinel on the output band |
| b1e33b126 | rs:sar_polsar_decompose — sentinels ingested in covariance windows (row 64) | per-channel sentinel skip + NaN declaration + noDataSamples counter |

### New test suites (5 targets, light lane: Catch2 + Qt6::Core + sicnu_operators + sicnu_processing; select with `ctest -R "^r4::"`)

- `test_operator_nodata_semantics` (7 cases / 254 assertions) — closed-form
  NoData exclusion for zonal/focal/mask/qa + defect regressions.
- `test_operator_chain_tolerance` (3 / 504) — MTL DN→radiance→NDVI chain
  (gain cancels, add does not: 1/(2(r+c)+7)); change
  difference/normalized-difference/CVA closed forms; Horn slope=atan(2)° /
  aspect=270° on z=2x with the kernel's real NoData-fallback contract.
- `test_operator_determinism_digest` (12 / 129) — 14 operator products
  byte-identical (or sha256 for CSV) across double runs; 300×300 grids
  cross 256-tile boundaries.
- `test_operator_preflight_refusals` (5 / 86) — typed RSOperatorError
  contracts: band range, CRS-less raster, empty vector, undeclared NoData,
  even window, missing wavelength axis, missing file, 1-band PCA, mask-CRS
  mismatch, missing QA roles.
- `test_known_answer_corpus_r4` (8 / 269) — operator-level analytic truths:
  threshold ≥ contract, σ⁰=DN²/A², convex-hull continuum removal, linear
  resample, SAM labelling + declared sentinel, last-valid-wins mosaic,
  verbatim extract, explicit no_data declaration.

**Total: 35 cases / 1,242 assertions.**

### Audit artifact

`NODATA_SEMANTIC_MATRIX.md` — **115 operator rows** across all rs families,
each with declared-semantics reading point, measured behavior (file:line at
the baseline), classification (参与统计/置NoData/排除统计/报错/未定义), and
disposition (fix commit / fixation test / backlog). Plus BASELINE / PLAN /
DECISIONS / EVIDENCE / REVIEW_LOG in `.planning/rs-operator-oracles-r4/`.

## Verification (local, double-run; CI not awaited per track rules)

- Fresh build dir `build-r4` (Debug, ENABLE_TESTS=ON), ninja 1.12.1
  **capped at -j2**, QT_QPA_PLATFORM=offscreen.
- `ninja -j2 sicnu_processing` → exit 0; five test targets → exit 0.
- **Potency**: with `src/` reverted to the pre-fix commit, the four defect
  regressions are red; fixes restored → green. (The similarity case needed
  a positive declared sentinel (+255) to be discriminating — negative
  sentinels are caught by the kernel's reflectance-like guard; recorded in
  REVIEW_LOG.)
- Gate, two consecutive passes post-review:
  `ctest -R "^r4::" -j1` → **35/35 passed, exit 0** (15.56 s)
  `ctest -R "^r4::" -j1` → **35/35 passed, exit 0** (13.18 s)
- Independent adversarial review (read-only subagent, pass 1):
  **SHIP-WITH-FIXES, 0 P0 / 0 P1 / 7 P2 / 4 P3 — all dispositioned**; pass 2
  re-verification recorded in REVIEW_LOG.md. The reviewer independently
  re-derived ten of the analytic truths and re-ran the suites.

## User-visible behavior changes

- `rs:spectral_derivative`, `rs:spectral_similarity`: declared-NoData pixels
  no longer produce garbage scores/derivatives.
- `rs:image_enhancement` (ratio), `rs:contrast_stretch`, `rs:band_ratio`
  (IHS), `rs:sar_phase_filter`, `rs:register_images`,
  `rs:sar_polsar_decompose`: output products now carry NoData declarations
  matching the holes they write; downstream readers can mask them.
- `rs:sar_polsar_decompose` additionally excludes declared sentinels from
  covariance windows and reports `noDataSamples`.

## Known limitations / backlog (documented, not silently dropped)

- Warp-family NoData contract (rs:resample / rs:align /
  rs:modis_georeference), SAR coregister resampling contract,
  interferogram CFloat32 declaration, OBIA OTB-engine sentinel contract,
  image_enhancement filter/speckle sentinel masking, per-band sentinel
  interface for the similarity kernel (DECISIONS D4/D4b).
- The brief's full test regex (~194 targets) was narrowed to `^r4::`
  because this worktree builds only the light closure; a full-regex run in
  CI before merge is recommended (DECISIONS D9).
- Local verification only; online CI not awaited (track rule).
