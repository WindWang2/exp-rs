# R18-B Task backlog (B01–B10 from task book; statuses updated as loops complete)

| ID | Subject | Status |
|----|---------|--------|
| B01 | Model identity & manifest truth: id/version/digest, weights path, task/bands/wavelengths/calibration/CRS, schema; bad values must not become "defaultable"; manifest↔provider shape/dtype consistency; catalog upgrades don't break old manifests | pending recon |
| B02 | Device enumeration & provider selection: CPU, CUDA:N, auto, device loss, VRAM shortage, provider unavailable, version conflicts; deterministic fallback ladder + original failure surfaced; no silent quality-changing downgrade | pending recon |
| B03 | session/pool lifecycle: concurrent init, borrow/return, context teardown, reload, cancel exit, worker crash recovery, GPU lease; barrier/latch-based deterministic races (no sleep-only tests) | pending recon |
| B04 | Tiling/overlap/merge: tile/stride/halo variants, boundary objects, empty tiles, overlap pixels, 100k logical extent; labels/masks/confidence/detection full-image assembly semantics vs small-input ground truth | pending recon |
| B05 | Detection & instance dedup: NMS threshold/IoU extremes, cross-tile duplicates, coordinate projection/clip, postprocess order, stable sort; deterministic vector outputs; CRS purity | pending recon |
| B06 | Inference cancel/timeout & contamination recovery: cancel/failure injection at read-tile, provider run, GPU alloc, publish stages; no half-published products / permanent locks / orphan GPU sessions | pending recon |
| B07 | Artifact & evidence integrity: mask/confidence/vector sidecar atomic publish, content hashes, model provenance, version consistency; r15-C recovery boundaries respected (no second recovery protocol) | pending recon |
| B08 | Layered perf baseline: preprocess, copy, provider invoke, postprocess, IO/flush, VRAM peak, CPU RSS; repeat on locally real CPU/accelerator; NOT_EXECUTED where hardware absent | pending |
| B09 | Type/shape/numeric defense: NaN/Inf logits, invalid output shapes, overflow, color-index bounds, mixed dtype, provider contract violations → typed rejection, no OOB | pending recon |
| B10 | Terminal loop: real interfaces of existing `rs:infer/segment/detect/embedding` (CLI) with accessible test models; legal mocks/fixtures only where weights/GPU absent; gap disclosed | pending |

Loop protocol per work package: Recon → RED → root cause → minimal fix → GREEN ×2 → perf/stability contrast → independent review → fix → re-review. One attributable change per loop, logged in LOOP_LEDGER.md.
