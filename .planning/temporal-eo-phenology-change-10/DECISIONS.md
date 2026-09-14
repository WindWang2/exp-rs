# DECISIONS — temporal-eo-phenology-change-10

Format: decision → alternatives → taken default → why. Autonomy=full: no user asks.

## D1 New operator `rs:temporal_regularize` instead of extending `rs:temporal_gap_fill`
- Alternatives: (a) extend gap_fill with calendar params; (b) new operator.
- Taken: (b). Gap fill's contract is "same calendar as input, fill between
  real observations" (`temporal_gapfill.h:5-19`); regular resampling changes
  the output calendar and provenance model (every output value synthetic or
  aggregated). One operator per observable behavior keeps schemas honest;
  the fill math is shared through the same interpolation rules where they
  overlap (linear bracketing).

## D2 Honest method naming for the joint model
- Taken: operator id `rs:temporal_harmonic_breaks`; descriptions say "greedy
  per-segment harmonic + linear-trend segmentation (BSFAST/CCDC-inspired;
  not the full BFAST/CCDC algorithms)". No claims of full BFAST (no iterative
  season-trend alternation) or CCDC (no L1/model selection).

## D3 Whittaker regularization defined on the calendar grid
- Alternatives: (a) continuous binned smoother; (b) penalized fit with data
  mapped onto calendar nodes.
- Taken: (b). The existing banded pentadiagonal solver solves z on a uniform
  grid (Eilers 2003); mapping observations onto nearest calendar nodes gives
  a well-defined, O(T) regularized estimate evaluated exactly at calendar
  points, plus a natural `max_gap_nodes` bridging guard.

## D4 Extrapolation stays a refusal everywhere
- Calendar points outside the observation span remain NaN for all methods.
  Rationale: gap-fill already refuses extrapolation (`temporal_gapfill.h`
  contract); consistency beats convenience, and provenance stays truthful.

## D5 Multi-ROI extraction streams by date, not by region
- Alternatives: (a) per-region scene reads (simple, O(R·T) opens); (b) one
  date pass accumulating all regions.
- Taken: (b). Scene handles are opened once per execution
  (`TemporalTileReader` precedent); region accumulators are O(R) (100k
  regions ≈ a few MB); each scene file is read exactly once, and only ROI
  windows are touched. Median keeps a bounded per-region buffer with a
  declared budget guard.

## D6 Region feature artifact is a plain CSV + JSON schema sidecar
- Alternatives: (a) register into the Dataset store (src/dataset); (b) CSV +
  sidecar.
- Taken: (b) for this track. The Dataset store is another platform's
  ownership; a stable-schema table with a typed sidecar is consumable today
  (join by `region_id`) and can later be ingested by the foundry without
  format change.

## D7 `rs:temporal_monitor` T-1 fix is schema + required-list, not logic
- Verified at baseline: monitor already routes through `prepareTemporalRun`
  (which parses `scenes` and `collection`), but its schema omits `scenes` and
  requires `collection`. Fix: declare `scenes`, relax required to
  `(output, method)` with collection-or-scenes validation.

## D8 No GUI dialog changes
- New operators surface through the existing schema-form/agent/CLI seams
  (ADR 0120/0131). A dedicated temporal dialog upgrade is a follow-up.

## D9 Phenology multi-cycle = caller-declared windows or two-peak auto split
- `cycles_per_year` ∈ {1,2}. Cycle 2 window defaults to the complement of the
  classic window; `auto` splits at the inter-peak minimum (documented
  heuristic, `splitConfidence = 0` when the year is single-peaked). The
  kernel stays hemisphere-neutral (windows are caller doy).

## D10 pybind11 offline configure
- Configure fetch of pybind11 failed offline; reconfigured with
  `-DFETCHCONTENT_SOURCE_DIR_PYBIND11=<main build>/_deps/pybind11-src`
  (local copy at identical v2.13.6 tag). No CMake change.
