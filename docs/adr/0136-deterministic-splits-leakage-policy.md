# ADR 0136: Deterministic Splits & Leakage Policy

- Status: Accepted (2026-09-07)
- Scope: `src/dataset` (split engine, leakage audit), `docs/datasets/data-leakage-policy.md`
- Depends on: ADR 0134/0135
- Ownership: Track D

## Context

The only split today is `RsClassificationSplit::stratifiedSplit` over
in-memory matrices: seed hard-defaulted, results unpersisted, groups barely
modeled, and no notion of spatial/temporal leakage. For remote sensing this
is the single largest scientific-validity hole: overlapping patches from the
same scene, polygons from the same object, or pre/post pairs routinely
cross train/test boundaries and inflate accuracy invisibly.

## Decisions

1. **Determinism by construction, not by convention.** All randomness runs
   through a self-contained PRNG (splitmix64 seeding + PCG-style output +
   Fisher–Yates), independent of `std::mt19937`/`std::uniform_int_distribution`
   implementation-defined behavior, so the same (DatasetVersion, SplitConfig,
   seed) replays byte-identically on MSVC/libstdc++/libc++. Every stochastic
   step records its seed policy (`Strict|BestEffort|NonDeterministic` +
   reason) in the manifest.
   - Rejected: `std::mt19937 + std::shuffle`. The standard does not pin the
     distribution algorithm across standard libraries; a reproducibility
     foundation must not depend on the vendor's runtime.

2. **Split engines (v1 set).** `random`, `stratified` (by label), `grouped`,
   `spatial_block` (grid blocks as groups), `spatial_buffer` (exclusion
   distance around test), `temporal` (by time/group), `leave_one_region_out`,
   `leave_one_scene_out`, `leave_one_year_out`, `k_fold`, `spatial_k_fold`,
   `group_k_fold`. Output is a persisted, versioned `SplitManifest`:
   config, seed, policy fingerprint, assignment per sample (train/val/test,
   fold ids), leakage audit summary, canonical fingerprint. A split without
   a stored manifest is not a split the platform will reason about.
   - Rejected: recomputing splits on the fly from config. Deterministic
     replay makes recomputation *possible*, but persisted manifests make
     assignments *auditable* and make "which split produced this run"
     answerable without trusting re-derivation.

3. **Leakage audit is a first-class report, not a comment.** `LeakageAudit`
   run over a (dataset version, split manifest) checks: exact duplicates
   (content digest), overlapping patch windows (same or different splits),
   shared parent polygon/object, shared source scene, distance below
   threshold, buffer overlap, augmentation-parent leakage, pseudo-label
   parent leakage, temporal future leakage, same-event/same-temporal-group
   crossing, pre/post pair splitting. Output is typed
   `LeakageFinding {kind, severity, sample refs, evidence}` aggregated into
   a `LeakageReport` that the split manifest embeds (summary) and the store
   keeps in full. No audit ⇒ no claim: the platform never states "no
   leakage" without a recorded audit; it states "not audited".
   - Rejected: boolean "is_leaking" flag on datasets. Findings need kind,
     evidence and severity to be actionable; booleans get cargo-culted.

4. **Spatial predicates are geometric, self-contained.** The audit uses
   window intersections, WKT bounding-box/polygon predicates and planar
   distance on stored geometries — no GDAL/OGR geometry engine dependency in
   `sicnu_dataset` (a WKT point/polygon reader adequate for audit predicates
   lives in the module; full GEOS power stays with the app layer).
   Rationale: keeps the scientific core unit-testable and bounded;
   complex-geometry edge cases are validation findings, not crashes.

## Compatibility

- `RsClassificationSplit` remains for the legacy in-process path; a follow-up
  may delegate to the new engine. No signature changes.
- Split manifests are additive store rows; nothing existing migrates.

## Resources

- Pairwise overlap checks are index-assisted (grid bucketing by window
  bounds), not O(n²) over 100k samples — benchmarked in the scale test.
- Audits run bounded and cancellable (cooperative cancel checks per bucket).
