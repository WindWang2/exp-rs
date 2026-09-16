# F14 · Radiometric, Atmospheric & Surface Normalization 11.0

**Local evidence only; no online CI dependency.**

## P0 (out of scope, pre-existing on master)

`src/agent/data_platform_tools.cpp` does not compile on a fresh master (`a5b11b7f10`)
checkout — `BenchmarkService` used unqualified (D19 regression; masked in the main checkout
by a stale Sep-14 object). Already fixed on the parallel branch
`origin/zcode/multimodal-registration-11` (23b326a485/08ebe07270); this PR deliberately does
NOT carry that fix. No radiometric-physics-11 target depends on `sicnu_agent`.

## Baseline & dedupe

- Baseline: `origin/master` = `a5b11b7f10` (prompt snapshot `ebcafb4d02` was stale).
- Open PR #1008 (`zcode/radiometric-spectral-workbench`, 45 files) owns the layer-level unit
  FSM (`exp_radiometric::RadiometricState`), a typed calibrator appended to
  `radiometric_calibration.{h,cpp}`, and a concrete 6S LUT. **This PR touches none of those
  files, adds no second unit FSM, and implements no radiative-transfer model.** Full file-level
  ownership table: `.planning/radiometric-physics-11/PARALLEL_OWNERSHIP.md`.
- Pre-existing master capabilities (calibration, DOS/QUAC, topographic correction, QA masks,
  ADR 0114 state vocabulary) are consumed, not duplicated; work package D (terrain
  illumination) needed no work beyond consuming solar geometry.

## What this PR delivers

New modules (all new files; kernels are pure C++ over doubles/float buffers):

1. **`SolarGeometry`** (`src/processing/algorithms/solar_geometry.*`) — sun
   zenith/elevation/azimuth, declination, equation of time, earth-sun distance and the
   inverse-square E₀ factor from acquisition date/UTC-time/scene centre (Spencer 1971 / NOAA
   closed forms; ~0.05°; no refraction). Fail-closed on invalid inputs. The first on-platform
   *computation* of sun geometry — previously angles only came from scene metadata.
2. **`RadiometricTransition`** (`radiometric_transition.*`) — conversion-legality authority
   over the ADR 0114 string vocabulary: the DN→radiance→TOA→surface DAG + thermal branch,
   every edge bound to its required inputs (MTL coefficient flags, real sun elevation, ESUN,
   K1/K2, atmospheric provider) with stable missing-input tokens, plus a versioned
   `exp_rs_radiometric_provenance/1` record (chain, formulas, coefficients, angle source
   metadata/computed, numeric scale before/after). Inversions and unit-jumping shortcuts
   refuse.
3. **`AtmosphericProvider`** (`atmospheric_provider.*`) — the optional provider seam: abstract
   provider with declarative auxiliary-input requirements, mutex-guarded non-owning registry,
   built-in `dos1`/`dos2`/`quac` adapters over the house kernels. Unregistered ids are typed
   refusals naming the alternatives — never silent DOS fallbacks. A future 6S-class LUT
   registers without platform changes.
4. **`BrdfNormalization`** (`brdf_normalization.*`) — Ross-Thick + Li-Sparse-Reciprocal
   kernels (canonical Lucht et al. 2000 form, (h/b)=2, (b/r)=1), anisotropy-factor
   normalization to nadir reference with caller-provided per-band weights, plus a
   mean-preserving two-date empirical c-factor (`c = mean(ref)/mean(target)`, exact identity)
   with explicit validity conditions.
5. **`RadiometricQa`** (`radiometric_qa.*`) — frozen uint16 flag vocabulary (saturation,
   negative, over-range, non-finite, cloud/shadow/snow, QA_RADSAT bits, invalid angles),
   union-only propagation, per-band summaries with explicit denominators, and linear
   gain/bias uncertainty propagation.

Operators (house RSOperator pattern, streaming, registered in `rs_operators_init.cpp` +
agent capability catalog): `rs:solar_geometry` (result JSON + optional in-place
`SICNU_SUN_*`/`SICNU_EARTH_SUN_*` stamping), `rs:brdf_normalization` (kernel-driven; angles
from parameters or `SICNU_*` metadata with typed refusal when absent; per-band NoData
sentinels mapped to NaN; radiometric state preserved and re-stamped),
`rs:radiometric_qa` (uint16 flag bands, schema `exp_rs_radiometric_qa_flags/1`, cloud-mask
grid-compat refusal, band-aware RAM estimate).

Docs: `docs/processing/radiometric-physics-11.md` (formulas with citations, contracts,
vocabulary tables), foundation-5 operator rows, CHANGELOG entry.

## Tests (independent oracles, negative coverage)

Five Catch2 executables, 49 cases / ~1100 assertions: published seasonal extrema and EoT
bounds, Cooper cross-formula (≤1.5°), noon-elevation identity, mirror symmetry, scripted
kernel references (k_geo(0,0)=0, k_geo(45,45,0)=+0.58579), linear-pair c-factor identity,
hand-computed Chavez/σ² algebra, an independent transition adjacency table, registry/refusal
contracts, and operator E2Es over synthetic GDAL rasters (registry wiring, metadata
round-trips, sentinel/mask handling, grid-mismatch refusals).

Local gate (final rebase, consecutive double run): **49/49 × 2 = 100%**. Regression on the
neighboring master suites (atmospheric 37486 asserts, radiometric_calibration 195688,
topographic_correction, qa_mask, satellite_products): **all green**.
`test_spatial_contracts` not-executed here (links the pre-broken `sicnu_agent`, unrelated to
this diff — see P0 note).

## Review

Two independent read-only reviews (science audit + final adversarial), all findings
dispositioned in `.planning/radiometric-physics-11/REVIEW_LOG.md`. Highlights fixed: the
Li-Sparse-R closing term (canonical `½(1+cosξ)secθs·secθv`), E₀ formula placement in
docs/provenance, provider-registry ownership (segfault), multi-edge transition planning,
QA_RADSAT silent no-op, mean-preserving c-factor semantics, sentinel hygiene, doc/agent
metadata drift. Final verdict: P0 = 0, P1 = 0.

## Known limitations / follow-ups

- Kernel weights are always caller-provided (single scenes cannot estimate them); the
  empirical pair path is a module API — a streaming `rs:*` wrapper is a natural follow-up.
- The provider seam ships no radiative-transfer provider (owned by the spectral-workbench
  effort); built-ins are the image-space DOS/QUAC family.
- Below-horizon suns are stamped for traceability; downstream validators refuse to calibrate
  with them.
- E0/declination series ignore leap years (standard RS convention, sub-0.01° effect) and no
  atmospheric refraction is applied (calibration geometry).
