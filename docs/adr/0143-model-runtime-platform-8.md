# ADR 0143: Model Runtime & Multimodal EO Inference Platform 8.0

- Status: Accepted (Model Runtime 8.0 goal series)
- Context: Platform 7.0 (ADR 0142) delivered the unified seam, N-D named
  transport, the deterministic device planner, closed-vocabulary manifests
  and the external-provider wire contract — but its ONNX Runtime provider
  had NEVER been compiled (no local build enabled
  `SICNU_WITH_ONNX_RUNTIME`), grid co-registration ignored the CRS,
  temporal semantics were limited to the channel fold with a manifest-fixed
  T, no production surface could reach the multi-input path at all, and no
  provenance record accompanied inference products.
- Decision:
  1. **Real ORT lane**: compile the 7.0 provider against the official ORT
     1.20.1 SDK (both `include/` layouts discovered). Real execution
     surfaced three latent defects — the CUDA-EP int overload, a dangling
     `TypeInfo` shape view in `warmup()` (the chained temporary died before
     `GetShape()` ran), and the removed `const T*` `CreateTensor` overload —
     all fixed; head selection now pushes the requested output name into the
     Run request so multi-head graphs work on the cv::Mat fast path. CUDA
     remains capability-gated: a forced-CUDA acquire on a CUDA-less host
     fails typed, it is never claimed as executed.
  2. **Grid/CRS authority**: co-registration is a geodetic verdict — size,
     geotransform (1e-6) AND semantic CRS equality
     (`OGRSpatialReference::IsSame`; WKT and authority codes both accepted).
     `alignment: "reference"` strictens to refusing CRS-less feeds. The
     runtime still never warps: preparation is an explicit caller step
     through the geospatial seam, recorded via feed `prepared_from`.
  3. **Provenance sidecars**: every published raster product gains
     `<output>.prov.json` (`exp-rs-prov/1`) through the same staged-rename
     publish; sidecar failure removes the product (no untracked results).
     Truthful by omission — unknown fields stay absent.
  4. **Temporal sequence lane**: `temporal_collapse: "sequence"` +
     `layout: "NCTHW"` feeds an explicit rank-5 time axis;
     `temporal_dynamic` lets the feed define T (≥ 2); feed timestamps are
     validated strictly increasing (refusal, never a silent sort);
     per-frame quality masks turn invalid pixels into NoData across the
     full window (coverage gate + zero-fill semantics). `rs:infer` gains
     `named_inputs[]` (the first production surface for multi-input) with
     local STAC collection/item expansion; remote STAC refuses — network
     STAC stays with the app-layer client.
  5. **Device planner 2.0**: `DevicePlacementPolicy` (LowestFitting default |
     LeastLoaded) — a placement knob only; admission, the bounded pressure
     valve and typed refusals are untouched. `deviceReport()` publishes the
     per-device capacity/reserved/holders snapshot (no sub-allocation, so
     free = capacity − reserved is the complete truth).
  6. **Pre/post completion**: `postprocess.class_mapping` remaps model
     classes to product classes for labels products (injective, non-negative,
     arity-checked against the head's planes; palette keyed by product ids).
  7. **Provider resilience**: the Python worker handshake may declare
     `capabilities` (max_rank, multi_input, dtypes) replacing defaults; a
     worker that DIES mid-exchange gets ONE respawn + replay per session
     (dead workers only — a live-but-stuck worker is a hang and is never
     restarted, so a request is never replayed into a live worker and there
     are never two delivered responses; a worker that dies after executing
     but before responding can still cause a replay — inherent to
     at-least-once recovery and honestly documented); exhausted budget is a
     typed ProviderCrash. The `exp-rs-infer/1` wire contract is extended
     additively, never forked.
- Consequences:
  - Adding a real backend lane is now an evidenced, repeatable act: the ORT
    lane has 11 green test cases (2168 assertions) including named N-D
    multi-input inference, dynamic shapes, dtype lanes, in-forward
    cancellation (~25 ms latency) and warmup/health/memory honesty.
  - Agents can rank and run multimodal/temporal models through one operator
    surface; misaligned inputs are refused with provenance-tracked verdicts
    instead of silently warped.
- Non-goals: distributed/multi-host execution, training, implicit warping,
  a second runtime/catalog/scheduler, network STAC inside operator params.
