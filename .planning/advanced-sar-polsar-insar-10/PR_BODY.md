# feat(sar): Advanced SAR / PolSAR / InSAR Scientific Platform 10.0

> **Local evidence only; no online CI dependency.** All claims below map to
> local commands + exit codes recorded in
> `.planning/advanced-sar-polsar-insar-10/EVIDENCE.md`. No GitHub Actions run
> was triggered, awaited, or cited.

## Baseline

- Branch `zcode/advanced-sar-polsar-insar-10`, worktree
  `../exp-rs-advanced-sar-polsar-insar-10`, cut from `origin/master` @
  `7d78059d1a6d316d606656759a506d17bc5e3b55` (verified current at PR time —
  rebase was a no-op). 9 commits, master untouched.
- Dedupe pass: no SAR branch/worktree in flight; the 40 most recent PRs and
  all open SAR issues (#854/#855/#929/#785/#803/#934/#330) are merged/closed —
  nothing here re-implements a merged fix. Existing 12 SAR operators and the
  orbit/zero-Doppler/forward-RD geocoding chain (Scientific Algorithms
  7.0/8.0) are consumed, not redone.

## Architecture

New kernels (`src/processing/algorithms/sar/`, each with the contract stated
in its header and pinned by known-answer tests):

| Module | Contract highlights |
| --- | --- |
| `sar_complex` | One SLC channel = one CFloat32 band; channel identity via `SICNU_SAR_COMPLEX_CHANNELS` metadata + explicit band params; invalid samples normalized to (NaN,NaN); halo edge-replicating complex tile stream (never the float BIP path — GDAL's complex→float would drop phase) |
| `sar_hermitian3` | Deterministic cyclic-Jacobi 3×3 Hermitian eigensolver (fixed sweep order, fail-closed convergence, no new dependency — OpenCV's `cv::eigen` is real-symmetric only) |
| `sar_polsar` | Reciprocal full-pol model: ensemble covariance, Pauli powers, Cloude–Pottier H/A/α (rank-1 ⇒ H=0, A=NaN — honest), Freeman–Durden, Yamaguchi with an explicitly defined helix model; model matrices ARE the contract, SPAN conservation test-pinned |
| `sar_insar` | Window coherence, interferogram phasors, Goldstein–Werner spatial filter, streaming IQR-clipped ramp fit (deterministic LCG reservoir), quality-guided reference unwrapper (per-component seeding, explicit provider seam), LOS displacement + Itoh diagnostic, patch-NCC coregistration shift, bilinear complex resample; cancellation probes inside the full-plane kernels |
| `sar_temporal_events` | ISO-8601 UTC acquisition contract (strict calendar + month-length validation), floating-day arithmetic (irregular revisit is the normal case), event-dating kernel sharing the upper-median baseline convention with `sar_temporal` |
| `sar_orbit` (append) | `interferometricBaseline`: B∥ / B⊥ / \|Δr\| from two platform positions and a unit LOS |

New operators (`rs:` family, registered append-only in
`rs_operators_init.cpp`): `rs:sar_polsar_decompose`, `rs:sar_interferogram`,
`rs:sar_phase_filter`, `rs:sar_unwrap`, `rs:sar_displacement`,
`rs:sar_coregister`, `rs:sar_temporal_events`.

## Major deliverables

1. **Complex/SLC artifact layer** (closes the "complex squeezed into float
   bands" gap): typed CFloat32 path end-to-end (read → kernels → stream out).
2. **PolSAR core chain**: `rs:sar_polsar_decompose` with pauli / h_alpha /
   freeman_durden / yamaguchi products (fixed band orders declared as
   `SICNU_SAR_POLSAR_BANDS`); dual-pol detected inputs are refused
   (`POLARIZATION_MISMATCH`) — no dual-pol approximation is relabeled as
   quad-pol.
3. **InSAR base chain**: `rs:sar_interferogram` (same-grid preflight via the
   #929 `compareGrids` authority, complex interferogram + coherence, optional
   robust flat-earth ramp) → `rs:sar_phase_filter` → `rs:sar_unwrap` →
   `rs:sar_displacement`; `rs:sar_coregister` for residual global-shift
   refinement. Closed end-to-end on synthetic SLC pairs (phase → displacement
   matches −λφ/4π in closed form).
4. **Multi-temporal SAR with real time semantics** (closes ISSUES.md S-1):
   `rs:sar_temporal_events` refuses to emit index-only products
   (`ACQUISITION_DATES_MISSING` / `DATES_NOT_ASCENDING`); event flags,
   first/last event scene+day, argmax days; `rs:sar_temporal_stats` gains an
   additive `dates[]`/`timeSemantics` result echo with its fixed band order
   byte-identical.
5. **Agent-facing knowledge**: 7 authored capability sidecars (ADR 0146 flow,
   `gen-pages --check` zero diff), `data/agent/capabilities/sar.json` entries,
   `docs/processing/sar-domain.md` §6–§9 contract chapters.

## Compatibility

- Existing operators, band orders (`SICNU_SAR_TEMPORAL_BANDS`), metadata keys
  and tests unchanged; `rs:sar_temporal_stats` change is strictly additive
  (schema `dates` param + result JSON echo; silent legacy fallback when dates
  are unavailable).
- New metadata keys are additive: `SICNU_SAR_COMPLEX_CHANNELS`,
  `SICNU_SAR_ACQUISITION_UTC`, `SICNU_SAR_WAVELENGTH_UM`, `SICNU_SAR_POLSAR_*`,
  `SICNU_SAR_INSAR_*`, `SICNU_SAR_TEMPORAL_EVENT_BANDS`.
- Registry/CMake/test-CMake edits are append-only integration commits;
  failure-mode vocabulary extended append-only (`COMPLEX_BANDS_REQUIRED`,
  `ACQUISITION_DATES_MISSING`, `DATES_NOT_ASCENDING`,
  `UNWRAP_PROVIDER_UNAVAILABLE`); `capability_knowledge_tool` descriptor-count
  guard bumped 111 → 121 (drift canary maintenance); contract graph snapshot
  regenerated (873 nodes / 279 edges / findings 0).

## Tests

New suites: `test_sar_complex` (378 assertions), `test_sar_polsar` (143),
`test_sar_insar` (266), `test_sar_temporal_events` (90),
`test_sar_platform10` registry E2E (2766 — exact PolSAR products, full InSAR
chain closure, coregistration recovery, ramp amplitude/phase contract, event
dating at day 24, refusal matrix, 256×256 streaming scale check).
Regression: all existing SAR suites pass unchanged (kernels 89, operators 595,
orbit 57, geocoding 5697, foundation5 279, temporal_stats 238) plus
`test_capability_contract_9` (2013) and `test_contract_platform_9`.
Command: `QT_QPA_PLATFORM=offscreen <binary>` per suite, serially; matrix and
outcomes in EVIDENCE.md. Build: `cmake --preset dev-default`, `-j2` cap, RSS/load
watched.

## Performance / resource

- Tile-level operators are O(tile + window) with hard parameter ceilings
  (windowSize ≤ 101, coherenceWindow/filter window ≤ 33, searchRadius ≤ 64,
  patchSize ≤ 128); twin-stream reads follow the single-threaded GDAL
  discipline of the existing N-scene operators.
- Plane-bound kernels are budget-gated with full inventory arithmetic (unwrap:
  21 B/px planes + quality + queue allowance ≤ 2 GiB; coregister: 24 B/px
  ≤ 4 GiB) and a compute-complexity guard on the NCC lattice (> 2e10 magnitude
  ops refused with guidance). Cancellation probes run inside both full-plane
  kernels and every streaming loop.
- Determinism: all kernels bit-exact (fixed scan orders, fixed-seed LCG
  reservoir, deterministic priority-queue tie-breaks).

## Review findings (Phase 7, two read-only subagents; log with dispositions in
`REVIEW_LOG.md`)

- P0: **0** (both reviewers).
- Subagent B (perf/concurrency/test-credibility): 3×P1 — ramp reservoir was a
  first-N truncation (fixed: deterministic LCG reservoir); unwrap budget
  under-counted planes/queue (fixed: full inventory); full-plane kernels were
  not cancellable and NCC params could escalate unboundedly (fixed: cancel
  probes + complexity guard). 2×P2 fixed (O(n) seed list; multi-tile halo test
  added), P3s fixed/documented.
- Subagent A (architecture/science): all core formulas verified correct by
  derivation. 3×P1 — flattenRamp amplitude double-counted |s2| (root-caused
  with a standalone repro after an intermediate fix was still wrong; final
  semantics: flattened master = master·e^{−i·ramp}), the promised
  `assumeReciprocity` refusal was missing (implemented:
  `NON_RECIPROCAL_CHANNELS`), ramp pre-pass halo index shift (fixed).
  P2s fixed (baseline geometry delivered per D-011; all-NaN quality unwrap now
  refuses), P3s fixed or amended in DECISIONS.
- All P0/P1 findings from both reviewers are fixed and covered by regression
  tests; accepted debts are listed with reasons in REVIEW_LOG.md.

## Known limitations (honest scope, also in operator metadata + docs)

- `flattenRamp` is a low-order flat-earth approximation — NOT DEM/orbit
  topographic phase removal. No atmospheric correction. No PSI/SBAS.
- The built-in unwrapper is a reference (not residue-aware, not a global
  optimum); single-scale plane unwrapping behind a 2 GiB budget; external
  providers must be registered or the operator refuses.
- `rs:sar_coregister` is a global-translation refinement only.
- Freeman–Durden/Yamaguchi clamp negative residuals to 0 (documented SPAN
  break near the noise floor); the refined (2012) orientation-adapted Yamaguchi
  volume is not implemented.
- Product-format parsing (SAFE/CEOS unpacking) stays with its owning track;
  complex channels enter through the generic GDAL CFloat32 contract.

## Pre-existing failures on master (not from this branch, out of scope)

`test_capability_drift` (3 canaries: cartography tools + product-import
knowledge entries + recipe alias — entries absent from master's
`tools.json`/`io.json`, verified via `git show origin/master`) and one
`test_mapspec` cartography-visual SIGABRT (`classification-a4l`; zero file
overlap with this diff). Evidence in REVIEW_LOG.md.

## Follow-ups

1. Baseline-aware interferometry (wire `interferometricBaseline` into
   interferogram result JSON when orbit states are declared).
2. DEM/orbit topographic phase removal as the rigorous successor to
   `flattenRamp`.
3. External unwrap provider registration (SNAPHU/MCF adapter behind the
   existing provider seam).
4. Sliding-window (O(window)/pixel) ensemble for the PolSAR operator.
5. Knowledge entries for the product-import operators / cartography tools
   (owners: respective tracks).
