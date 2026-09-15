# ADR 0163 — Spectral Intelligence 11.0: local RX, sparse unmixing, hybrid similarity, endmember analysis

## Status

Proposed (spectral-intelligence-11)

## Context

Master (baseline a5b11b7f) ships the Spectral 10.0 platform: global RX
(`rs:rx_anomaly`), OLS/FCLS unmixing (`rs:spectral_unmixing`), SAM/SID
kernels (`spectral_classification`), matched filter/ACE, streaming MNF,
PPI endmember extraction, the spectral-table artifact and the
library/reference operator seam (ADR 0076–0082, 0092, 0148). Four
capability gaps remain, enumerated by the 11.0 track baseline audit:

1. **Anomaly detection is global only.** RX against scene-wide statistics
   misses anomalies embedded in heterogeneous backgrounds; no local/dual
   window detector with guard semantics exists.
2. **Unmixing has no sparsity constraint.** FCLS requires nEndmembers ≤
   bands and produces dense abundance vectors; there is no interpretable
   L1-regularized path for overcomplete dictionaries, and no redundancy
   guard for near-duplicate atoms.
3. **SAM and SID exist only side by side.** There is no combined similarity
   (the classic and the bounded hybrid), and no wavelength-comparability
   guard on pairwise spectral comparisons.
4. **Endmember sets have no analysis layer.** PPI output is a raw ranked
   list: no clustering/deduplication, no pairwise SAM matrix, no
   sensor-projection step that carries provenance/licenses into the
   derived artifact.

Also contested: open PR #1008 (radiometric/6S workbench) rewrites
`spectral_unmixing.*` and `spectral_profile_widget.*`. This track must not
touch those files.

## Decision

Four new kernels, four new operators, one GUI panel — all in NEW files;
shared registration files receive append-only edits.

### A. Local / dual-window RX (`SpectralLocalRx`, `rs:local_rx_anomaly`)

* Outer window = local background, inner guard window excluded so a
  compact anomaly cannot contaminate its own statistics; windows clamp to
  the raster (no replicated border pixels in statistics).
* Covariance: sample covariance (N−1) + **scaled diagonal loading**
  `alpha * tr(Σ)/B * I` (proportional to data variance — predictable bias
  for small windows, unlike an absolute ridge). `covariance=diagonal`
  ("RX-D") covers high band counts; Full refuses above 8192 bands
  (fail-closed, not thrash).
* Honesty: pixels whose window has fewer valid background samples than
  the minimum (auto: 2B+2 full / B+1 diagonal) stay NaN with a per-pixel
  background-sample count plane; unscored ≠ zero.
* Operator streams padded tiles (halo = outer radius) so interior scores
  equal whole-raster scores (tile-agnostic determinism); out-of-raster
  padding reuses the band NoData → invalid-pixel predicate, matching the
  clamp semantics exactly.

### B. Sparse unmixing (`SpectralSparseUnmixing`, `rs:sparse_unmixing`)

* Problem: `min ½‖x−Ea‖² + λ‖a‖₁ s.t. a ≥ 0` (+ optional sum-to-one
  **penalty** ρ·11ᵀ — the same convention as the master FCLS, so the two
  solvers share one constraint vocabulary).
* Solver: FISTA with the exact prox of the separable nonsmooth part
  (`prox(v) = max(v−λ·step, 0)`); deterministic (fixed power-iteration
  Lipschitz estimate ×1.05, no randomization), per-pixel convergence
  flags + iteration counts reported honestly.
* Overcomplete dictionaries allowed (atoms > bands; cap 2048 atoms =
  32 MiB Gram bound). Near-collinear atom pairs (SAM below
  `collinearAngleDegrees`, default 0.5°) **refuse**: an L1 split across
  near-duplicate atoms is not interpretable. Threshold 0 is the
  documented escape hatch.

### C. SID-SAM hybrid (`SpectralHybridSimilarity`, `rs:spectral_similarity`)

* SAM/SID stay single-sourced in `SpectralClassification`; this layer adds
  only the combination: `product_normalized` (default; sam′·sid′ ∈ [0,1],
  cross-scene comparable) and `classic_tan` (SID·tan θ, unbounded,
  literature form). Form parsing is fail-closed.
* Wavelength guard: when both nm grids are provided they must be valid,
  equal-sized and overlapping (`SpectralWavelength::rangesOverlap`) —
  disjoint grids refuse instead of emitting meaningless magnitudes.

### D. Endmember analysis (`EndmemberAnalysis`, `rs:endmember_analysis`)

* `reduceEndmembers`: average-link agglomerative clustering on pairwise
  SAM distance (threshold default 2°); representative = highest PPI count
  (tie → lowest index, deterministic); caps 512 rows.
* `angleMatrix`: symmetric, zero-diagonal SAM matrix from the single SAM
  source of truth.
* `projectToSensor`: Gaussian-SRF (or linear) projection via the master
  `SpectralResampling` seam; wavelength metadata mandatory (typed refusal
  otherwise — no silent "pretend it's on the grid"); coverage flags +
  `requireFull` refusal.
* Artifact: output is a derived `exp-rs:spectral-table` — no second
  artifact authority; provenance.sourceOperator = this operator,
  license/citation follow the input (validate() enforces the measured
  table rules), digests echo input/output identity.

### F. Workbench panel (`SpectralWorkbenchPanel`)

Independent dock (hidden by default; Window menu toggle) — does not touch
D18 mission-workbench files nor the contested SpectralProfileWidget. The
panel loads a spectral table (fail-closed), renders the SAM matrix via the
11.0 kernel, shows provenance/digest/license status, and exposes
`spectrumSelected(label, index)` as the selection-linkage seam.

## Alternatives considered

* Ledoit–Wolf shrinkage for local RX — more theory, but the loading factor
  stops being a closed-form input; known-answer tests would need the same
  estimator reimplemented, weakening oracle independence. Scaled loading
  keeps the bias explicit and the oracle trivial. (DECISIONS D2)
* ADMM for sparse unmixing — comparable guarantees but two penalty
  parameters and a dual variable; FISTA needs only λ and reports
  convergence over the same objective. (DECISIONS D3)
* Weighted-sum hybrid similarity — no independent scientific motivation
  over the product forms; dropped. (DECISIONS D4)
* New artifact kind for endmember analysis — rejected: spectral-table
  already owns provenance/license/digest authority. (DECISIONS D6)

## Consequences

* The algorithm_meta catalog grows 32 → 36 sidecars (4 new task-declaring
  operators); the drift gate pins are updated in the same change and the
  shipped sidecars are regenerated via `--export-catalog`.
* capability JSON: four entries appended to
  `data/agent/capabilities/spectral_transform.json` with honest
  limitations (unscored pixels, penalty-style sum-to-one, caps).
* Existing operators/rasters stay untouched; `rs:rx_anomaly` remains the
  global detector, `rs:spectral_unmixing` the dense solver.
