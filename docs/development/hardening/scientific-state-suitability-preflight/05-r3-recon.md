# R3 Recon — scientific-state-suitability-preflight (hardening campaign 10/20)

- Date: 2026-09-25
- Base: `origin/master` = `9ea5a2fd17317d924ac2c234286650b48c0b02bf` (post #1315)
- Worktree: `exp-rs-hardening-sci-state-suit-preflight-r3`, branch `hardening/scientific-state-suitability-preflight-r3`
- Prior rounds: R1 = PR #1253 (closed, landed via #1258), R2 = PR #1289 (MERGED ab4ab981a1), preflight slice B = #1279 (MERGED).

## Dedup at start (2026-09-25)

- Open PRs: #1314 (models crash-orphan — other track), #1312 (workbench lifecycle — other track). No overlap with this track's files.
- Open issues: 0.
- No changes to `src/scientific_state`, `src/suitability`, `src/preflight` between R2 merge (ab4ab981a1) and base — clean slate for R3.
- R2-closed items NOT to redo: resolveAcquisition UTC normalization, catalog index-aligned pairing guard, offset bounds / midnight fraction / year rollover.
- R2 residual P3s NOT to redo: year-0000 proleptic spelling, sarFacts "dn" substring, resolver `.front()` single-source read, supervised=false (product decision), preflight slice B (owned by #1279, now merged).

## Scope of R3

1. `src/preflight` — post-#1279 adversarial hardening (engine/rules/mirror/adapter never independently reviewed after slice B).
2. `src/suitability` — first hardening pass ever (landed #1236, untouched by R1/R2).
3. Track goal #4: adapter-level consistency (same asset state must not contradict itself across passport/preflight).

## Findings (recon agents, evidence verified in source by main agent)

### Preflight
- **PF-1 (P1)** variant-parameterized policies silently drop when no variant matches: `capability_mirror.cpp` mergeEntry variant loop `continue`s on no match, returns Available; `rules.cpp` band-role gate sees `band_roles` missing → "no band_roles declared" → pass. Flagship mirror entry `rs:spectral_index` (only one of 247) keeps all band_roles in variants → single-band NIR fed to NDVI ⇒ verdict ok. Repro: pure std C++.
- **PF-2 (P1)** `kind="unknown"` resolved facts silently skip all raster rules: `isGriddedRasterKind` closed set + "non-raster slot skipped" (no finding); passport JSON may legally omit `identity.kind`; adapter maps `AssetKind::Unknown` → "unknown" Available. Contradicts "missing fact must be typed unknown" module promise. Repro: pure std C++.
- **PF-3 (P2)** extends chain deeper than kMaxMergeDepth=4 silently truncated, no problem recorded, healthy() stays true — contradicts header's fail-closed promise (verified by probe).
- **PF-4 (P2)** NaN bypasses numeric gates: `pixelSize <= 0` false for NaN → ratio NaN → comparisons false → require_ack SPF_RESOLUTION_MISMATCH instead of typed unknown (cloud cover same shape). Open provider seam reachable; canonical reader blocks production path.
- **PF-5 (P2)** request_digest omits `budgets` → same digest, different canonical bytes/truncation semantics.
- **PF-6 (P2)** ack matches by code only → one ack clears same-code findings across other slots/assets; mirror-unavailable/operator-unknown are require_ack (golden-pinned severity — keep severity, fix precision).
- **PF-7 (P2)** adapter drops passport lifecycle (missing/stale/error passports judged as observed), claim lattice (basis always rule constant "observed"), temporalRefsTruncated hardcoded false.
- P3 (recorded, fix if cheap): render trusts verdict-field consistency of hand-built/deserialized reports; report.h ADR-0174 citation rot; empty registry → ok; registry cap display; operatorParams non-object silently {}.

### Suitability
- **SU-1 (P1)** goal time window zoneless ISO → Qt::LocalTime (no UTC normalization; scene side normalizes and pins UTC). Cross-host verdict flip + same goalDigest with opposite verdicts. Repro: assert timeSpec after zoneless parse.
- **SU-2 (P1)** grid pair sampling linear stride tail blind spot: n=1000, max sampled linear=497002 but pairs of scene 999 occupy [498501, 499499] — entirely unsampled; sole bad scene at tail ⇒ Suitable with blocking_pair_count=0. Also inconsistent truncation posture vs labels (which demotes sampled pass to Marginal). Repro: pure unit test.
- **SU-3 (P2)** model.modality pinned but modality_pool empty → silent Suitable (GSD same situation → Unknown). Repro: unit test.
- **SU-4 (P2)** StoreDataProvider truthy() = lowercase {true,1,yes} only; facet value "True"/"Y" → fabricated zero → labels Suitable. Fail-closed direction: unknown truthy-ish tokens must not read as absent. Repro: unit test.
- **SU-5 (P2)** read-only assessment tool creates SQLite file via DatasetStore::open (CREATE flag) on typo path.
- P3 (recorded): "(other)" literal collision; DatasetFacts invalid temporal string silently survives; goal scalar wrong-type silently defaults; SceneCandidate extent/grid no finite gate; pixelSizeCrsUnits NaN; criteria_grid verdict_counts vs blocking_pair_count semantics; dead facts.crsWkt/extent fields; min_samples serialized as 200.0; isSameCrs authority-string noise.

## Fix plan (one change per loop round, each RED-first)

PF-1, PF-2, SU-1, SU-2, SU-3, SU-4 (P1s first), then PF-3, PF-4, PF-5, PF-6, PF-7 (+track-goal-4 consistency oracle), SU-5, SU-7/8, cheap P3s. Severity词汇/golden-pinned semantics are not product decisions — recorded, not changed.

## Build/verify lane

- `build-r3` (Unix Makefiles, Debug, -j2). Preflight lane: sicnu_preflight + adapter + scientific_state are Qt-free; suitability lane: Qt6::Core + Sicnu::data + dataset (no qgis_gui — GCC16 Qt-heavy ICE trap avoided).
