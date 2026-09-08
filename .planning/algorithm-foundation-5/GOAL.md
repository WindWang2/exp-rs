# Scientific Algorithm Foundation 5.0 — Goal

> Track branch: `zcode/algorithm-foundation-5` (worktree `../exp-rs-algorithm-foundation-5`)
> Baseline: `origin/master` @ `93a7fb0bbd` (Model Runtime & AI Inference Platform 4.0)
> Mode: unattended long-running epic. Local build/test/bench evidence only — never block on online CI.

## Mission

Upgrade the current "representative algorithm collection" into a systematic,
verifiable, reusable **Remote Sensing Scientific Algorithm Foundation 5.0**
without disturbing the 3.0/4.0 platform architecture (TaskCenter, JobEngine,
RSOperator framework, Model Runtime, Plugin SDK, Pi harness).

Three pillars:

1. **Primitives** — consolidate cross-algorithm scientific kernels
   (NoData, grid contract, statistics, histogram/percentile, convolution,
   morphology, connected components, distance transform, window policy).
2. **Family completion** — systematically extend Optical, Spectral, SAR,
   Temporal, Terrain, Raster-Spatial, Classification, Change Detection.
3. **Certification** — known-answer / synthetic / degenerate / execution test
   matrix per operator, performance & memory baseline, 5+ review lenses with
   P0/P1 = 0.

## Hard architecture constraints (unchanged)

```
UI → TaskCenter → JobEngine → RSOperator → kernel
```

- No new scheduler/registry/runtime; no GUI raster loops; no second catalog.
- No implicit resample / CRS change / pixel-size change — typed refusals.
- Reuse GDAL/OpenCV/OTB wrapped capability where it already exists.
- Every new capability ships as an `rs:` operator with schema, determinism
  grade, estimate, cancellation, output metadata, and validation fixtures.
- Build resources: Ninja `-j2` default, `-j1` under memory pressure;
  targeted `ctest -R <family> -j1`; never `-j$(nproc)`.

## Execution order (major milestones)

- **Phase 0** — baseline audit + capability matrix (this dossier).
- **A** — Scientific Primitives 5.0 (consolidation first, no forced moves).
- **B** — Optical / radiometric completion (topographic correction, index
  families, haze/DOS variants, spectral transforms).
- **C** — Spectral analytics completion (SID, matched filter, ACE, noise
  estimation, constrained unmixing audit).
- **D** — SAR completion (dual-pol feature foundation, range-Doppler
  executable subset with honest refusal contract).
- **E** — Temporal analytics (CUSUM, EWMA, seasonal MK, multi-season
  phenology, BFAST/CCDC-honest approximations, double cropping).
- **F** — Terrain/DEM (curvature, multidirectional hillshade, relief, flow
  foundation, viewshed decision).
- **G** — General raster spatial (resample/align operators, rasterize,
  sieve, proximity, zonal stats, fill holes, clump…).
- **H** — Classical classification (kNN, min-distance, Mahalanobis,
  max-likelihood, logistic, ISODATA; training-sample contract hardening).
- **I** — Change detection contract consolidation (reuse Milestone A kernels).
- **J** — Operator contract hardening (schema/catalog drift tests).
- **K** — Certification matrix per family.
- **L** — Performance/memory/scale baseline.

Then: 5+ review lenses → P0/P1 remediation → docs sync → coherent commits → PR.

## Completion gate (abridged)

Capability matrix audited; primitives consolidated; B–I families measurably
extended through the RSOperator seam; certification + perf evidence; targeted
tests green; review P0/P1 = 0; docs synced; branch clean; PR created. An
infeasible theoretical module (e.g. full range-Doppler RTC) is delivered as
executable subset + typed refusal + extension contract + tests + documented
limitation — never faked.
