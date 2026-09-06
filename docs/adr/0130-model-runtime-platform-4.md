# ADR 0130: Model Runtime & AI Inference Platform 4.0

- Status: Accepted (Model Runtime & AI Inference Platform 4.0 goal series)
- Context: Model Runtime 3.0 shipped a manifest catalog (v1/v2/v3), an LRU-2
  session cache keyed by artifact PATH, an OpenCV DNN provider, and a
  segmentation-flavored tile engine behind a single `rs:infer`. The gaps were
  structural: session identity did not include artifact bytes (a swapped weight
  file at the same path silently kept its old session), the runtime contract had
  no warmup/cancel/health/memory/shutdown, devices were a boolean cpu/cuda with
  no `auto` or `cuda:N`, detection manifests were rejected as unimplementable,
  and every failure mode mid-run could leave a truncated GeoTIFF at the
  caller's output path. A prior design sketch (`src/runtime/gpu/gpu_plane.h`)
  anticipated identity-keyed pooling but was never wired.
- Decision:
  1. **Content identity** (Phase 1): every artifact is SHA-256 hashed at
     catalog load (or acquire for ad-hoc file references), whether or not a
     checksum is declared. The session cache key is
     `framework | device | digest` — same path with different bytes can never
     share a session; equal bytes at different paths share one. Manifests gain
     `id` (stable identity, fallback `name`), `model_version`, `license`,
     `source` and `manifest_version` (declared version must match manifest
     shape). Unreadable artifacts fall back to path/size/mtime identity; no
     real provider can load unreadable bytes, so no stale-content session is
     reachable.
  2. **Unified runtime contract** (Phase 2): `IModelRuntime` gains
     `warmup()` (best-effort, never fatal), `requestCancel()`/`clearCancel()`
     (cooperative — a running forward pass cannot be interrupted), `health()`
     and `memoryEstimate()` (0 = unknown; estimates are never invented).
     `classifyInferenceError` maps failures to {OutOfMemory, Canceled,
     ShapeMismatch, CorruptModel, NotLoaded, Unknown} for diagnostics and the
     OOM ladder.
  3. **Deterministic devices** (Phase 3): `resolveDevice(request, hw, …)` is a
     pure function — `auto` resolves to the lowest addressable CUDA index that
     fits the VRAM budget, else CPU; explicit `cuda:N` beyond a backend's
     addressable index (opencv_dnn: 0) fails loudly instead of silently
     running elsewhere. The registry records per-provider `ProviderTraits`
     so readiness and acquire agree.
  4. **Authoritative tiler** (Phase 4): the raster engine publishes atomically
     (same-directory `.tmp~` stage + rename; failure paths abandon/remove the
     stage — the output path only ever holds a complete raster). OOM in a
     batch retries tile-by-tile (per-tile semantics unchanged); an OOM at
     batch=1 is terminal with a diagnostic. The engine never shrinks tiles,
     changes resolution, or alters model semantics to fit memory. A 100k×100k
     logical raster runs as 200×200 windows without whole-raster allocation
     (memory stays O(batch × patch)).
  5. **Declarative postprocess** (Phase 5): `output.format` selects
     `probability` (historical float32 stack), `labels` (argmax → Byte/UInt16
     raster + deterministic palette metadata), `mask` (binary 0/1) or
     `confidence` (top-1 score band). Conflicts (uncertainty + derived mode,
     mask_threshold + labels, multi-head + derived mode) fail at parse/run —
     the #646 declared-but-unenforced class stays closed.
  6. **Detection** (Phase 5): `output.detection` declares the decode
     (`xywh_objectness` v5-style / `xywh_class_scores` v8-style, tensor
     layout, thresholds, NMS IoU, bounded `max_detections`). The detection
     tile engine reuses the raster skeleton (window reads, preprocessing,
     bounded batches, OOM ladder, cancel) and publishes georeferenced boxes
     as GPKG/GeoJSON/SHP through the same atomic stage+rename. Tile-overlap
     duplicates resolve by a center-in-core-tile rule plus whole-raster NMS
     with a deterministic order. Declaring `postprocess.nms` or
     `output.threshold` is no longer a rejection when a detection contract
     exists (they fold into it); without one, the historical rejections stand.
  7. **Registry authority** (Phase 6): `ModelCatalog` gains
     `registerManifestJson` (session-scoped, shadows scanned entries),
     `unregister`, `inspect`, `validateManifestJson` (pure), `resolve("id@version")`
     and `health`. GUI/CLI/Workflow/Pi/SDK reference models by stable id;
     no surface hardcodes weight paths.
  8. **One execution seam** (Phase 7): `runModelInference(request, context)` is
     the only model execution path. `rs:infer` keeps its historical result
     payload keys and error-code mapping verbatim; `rs:segment`, `rs:detect`
     and `rs:embedding` are thin intent-flavored adapters — no engine, session
     or preprocessing code lives in an operator.
  9. **Bounded session pool** (Phase 8): the registry IS the pool — LRU
     capacity bound, optional idle eviction, per-key `release`, `PoolStats`
     (loads/hits/misses/evictions). Unload means "no longer handed out";
     live shared_ptrs stay valid until dropped.
  10. **Layering**: framework (catalog/contracts) never depends on runtime;
      `src/runtime/**` (execution plane) is untouched — its pool sketch is
      superseded by the operators-layer pool; the classical-ML classification
      pipeline (ADR 0019) stays an independent deep module (single predict
      path, no duplicated tiling).
- Consequences:
  - Detection models with an `output.detection` contract can now execute to
    their declared vector contract; the shipped `yolo-ship-detection`
    template carries the contract as the reference example (the other three
    YOLO templates still need theirs authored — they keep parsing but are
    rejected loudly at detection run time until then).
  - Failure behavior is testable property-by-property (corrupt bytes, OOM
    ladder, cancel latency, partial-write atomicity, unwritable paths,
    removed artifacts) — see `tests/test_model_failure_matrix.cpp`.
  - Honest limitations are documented, not hidden: a single forward pass is
    not interruptible (cancel lands at checkpoints), opencv_dnn cannot
    address cuda:N>0, and the ONNX Runtime provider remains conditional
    (`SICNU_WITH_ONNX_RUNTIME`) until the dependency ships in the baseline.
  - Remote (HTTP/Python-process) inference runtimes are NOT part of 4.0; the
    extension seam remains `ModelRuntimeRegistry::registerProvider` (used by
    the plugin bridge), and structured error/cancel semantics now exist for
    such providers to adopt.
