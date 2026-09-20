# ADR 0171 — EO Model Runtime 13.0: ensemble execution semantics

- Status: accepted (this track)
- Date: 2026-09-21
- Supersedes/extends: PR #1118 (EO Model Runtime 12.0 — manifest-defined raster
  ensembles, provider fallback chains, session-pool stress, tile equivalence)

## Context

Model Runtime 12.0 landed manifest-defined RASTER ensembles (weighted_mean /
weighted_vote over member probability stacks), provider fallback chains, the
session-pool stress oracle and the tile-semantic-equivalence oracle, and
explicitly typed-refused the remaining product surfaces: detection ensembles,
scene-classification ensembles, parallel member execution, compressed staging
and request-level derived outputs. 12.0 also executed members strictly serially
and staged member stacks as (LZW) GTiff intermediates with an UNCOMPRESSED
combine stage.

This ADR pins the semantics this track adds so every product is explainable:
what the combinations mean, in which frame detection boxes are fused, how
members execute concurrently, and what each surface refuses.

## Decision 1 — Detection ensembles: canonical Weighted Boxes Fusion (WBF)

Source: Solovyev et al., "Weighted Boxes Fusion: ensembling object detection
models for better accuracy" (arXiv:1910.13302); reference implementation
ZFTurbo/Weighted-Boxes-Fusion. Concatenation + NMS is NOT a fusion (it is
dedup) and is deliberately not implemented.

Frame: each member engine maps its decoded boxes back to RASTER pixel
coordinates itself (letterbox reverse transform `fedPx·(windowPx/inputPx)`
plus the center-in-core seam rule). The ensemble fuses in that one common
frame — all members ran the SAME input raster — and the final writer applies
the input's geotransform ONCE. The ensemble never re-derives coordinates.

Algorithm (`src/operators/runtime/detection_fusion.cpp`, pure and
unit-testable):

1. **Gate.** A box enters the fusion only when its RAW confidence ≥
   `ensemble.detection.skip_box_threshold` (default 0), its geometry is
   finite and positive-area, and its member weight is positive. DEVIATION
   from the reference (documented): the reference gates the
   member-weight-scaled score; we gate the raw confidence so a heavy member
   weight cannot smuggle a low-confidence box past the gate.
2. **Order.** Effective score = confidence × member weight. Pooled boxes are
   visited in a deterministic total order (effective desc, then classId, x,
   y, w, h asc) — never input order or addresses.
3. **Cluster.** Each box joins the same-class cluster whose CURRENT fused
   representative has the best IoU with it, when that IoU is strictly
   greater than `ensemble.detection.iou_threshold` (default 0.55); otherwise
   it opens a new cluster. Matching the live representative is what makes the
   fused coordinates the weighted average of the whole cluster.
4. **Fuse.** Coordinates: `Σ(effective·coord) / Σ(effective)`.
   Confidence: `mean(effective) × min(members, clusterSize) / Σ(member
   weights)` — the canonical count-aware rescale, so agreement ACROSS members
   raises the fused confidence relative to a lone detection (a cluster of one
   out of N members is scaled by 1/N).
5. **Publish.** Fused boxes in confidence-desc deterministic order. No
   post-fusion NMS (clustering already dedups). An empty result is a valid
   product.

Refusals: a member without a detection contract or with a different class
vocabulary (names AND order — classId indexes the vocabulary, so a silent
remap would relabel the product); a detection request on a non-`wbf`
combination; a `wbf` manifest on a non-detection request. Provenance records
algorithm, thresholds, pooled/gated/fused counts and per-member absorption
(`fusion` block in the sidecar).

## Decision 2 — Scene-classification ensembles

Input contract: each member's per-class probability vector, produced by the
SAME `classifyScene()` core the single-model artifact uses (one forward pass,
spatial mean-pool, `logit` → softmax / `probability` → clamped, never
silently renormalized). All members must declare the SAME vocabulary (names
and order) — mismatch is a typed refusal, never an implicit reorder.

- `weighted_mean`: `combined_c = Σ w·p_c / Σ w`; predicted = argmax, ties to
  the lowest index. When every member is logit-semantics the combined vector
  is a proper distribution; otherwise it is the weighted mean of the members'
  clamped scores — recorded, never renormalized.
- `weighted_vote`: each member votes its argmax with its weight; winner =
  highest accumulated vote, ties to the lowest index; agreement = winner
  vote share. `probabilities` still carries the weighted mean (well-defined
  under both combinations); `predicted_index` follows the combination.

Output: `exp-rs-classification/1` + an additive `ensemble` block (members,
weights, execution identity, per-member score semantics, fallback trail).
A `distance` head or a missing `output.classes` is refused per member.

## Decision 3 — Bounded parallel member execution

- Budget: manifest `ensemble.max_concurrent_members` ∈ [1,16]; default
  (absent) = auto = `min(memberCount, 4)`. The budget bounds CONCURRENCY only
  — the VRAM ledger remains the device-memory admission authority.
- Acquisition stays serial and member-ordered (the registry serializes
  admission; the ledger and provenance record that order). Parallelism starts
  only at RUN time and adds no sessions or reservations.
- One worker thread per member; a counting semaphore acquired INSIDE the
  worker bounds how many run at once ("parallel" never means "start
  everything").
- Fail-fast: the first worker failure (or parent cancellation) raises a
  shared stop flag; each worker polls it — together with the parent's
  cancellation — through its own CHILD context at every engine check point,
  and exits without publishing.
- Isolation: exceptions never escape a worker (no terminate); each captures
  its own `exception_ptr`. After joining all workers the main thread rethrows
  the LOWEST-INDEX real failure — the surfaced error is a deterministic
  function of the failure set, never of scheduling.
- Determinism: results are assembled strictly in member index order;
  completion order never reaches the product (serial vs bounded-parallel runs
  are bit-identical — oracle).
- No Catch2/Qt assertions on worker threads.

## Decision 4 — Staging compression

- The ensemble's own combine stage is created `TILED=YES, BLOCKXSIZE=256,
  BLOCKYSIZE=256, COMPRESS=DEFLATE` by default (the published product is what
  the user keeps; 12.0's uncompressed stage was a known disk-cost
  limitation). Manifest `ensemble.staging_compression` = "none" reproduces
  the historical bytes; "deflate" (default) is the improvement. Member stacks
  keep the tile engine's own writer.
- Evidence is machine-independent: same content, deflate vs none → the
  compressed stage is materially smaller AND every published value is
  identical. No wall-clock claims.

## Decision 5 — Request-level derived outputs on ensembles

Defined over the weighted MEAN only (the member preprocessing/task contracts
are already enforced per member; the collapse owns product semantics only):

- `labels`: argmax over the combined classes, ties → lowest index; Byte ≤255
  classes else UInt16 with the sentinel-excluded domain; class-name metadata
  when all members agree on the vocabulary.
- `confidence`: top-1 combined probability (float32).
- `mask`: 1-class → mean ≥ threshold; else argmax ≠ 0. Threshold = the
  ENSEMBLE manifest's `postprocess.mask_threshold` (default 0.5, mirroring the
  single-model engine).

Refused (documented): derived modes on `weighted_vote` except `labels` (the
vote product IS the label product with its agreement band); an explicit
`variance` band together with a derived mode (Byte product + float band
cannot share one GDAL dataset dtype — the same refusal the single-model
engine makes); class remapping / morphology / calibration (single-model
product knobs whose ensemble-level semantics are undefined).

## Decision 6 — Residue fix (found during the 12.0 audit)

12.0's `StagedFileGuard` tracked only the member stack; each member engine
also publishes `<stack>.prov.json`, which leaked on BOTH the success and the
failure paths. Sidecars are now tracked and removed on every path; the
zero-residue oracles cover stacks and sidecars.

## Consequences

- Every ensemble product can explain itself: identity/digest/framework/
  device/weight/execution stats/fallback trail per member, plus the fusion
  algorithm and thresholds (detection) or the combination and per-member
  score semantics (scene).
- Deterministic under concurrency; cancellation and one-member failure leave
  zero staged residue.
- Still refused (documented, not silently approximated): multi-feed/temporal
  ensembles, TTA, nested ensembles.
