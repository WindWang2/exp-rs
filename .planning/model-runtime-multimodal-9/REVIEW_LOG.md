# REVIEW_LOG — model-runtime-multimodal-9

Review passes: (1) main-agent self-review during/after implementation,
(2) read-only Reviewer A (architecture/correctness/concurrency/scientific
validity/security), (3) read-only Reviewer B (tests/performance/
portability/resource bounds/docs-vs-code). All P0/P1 fixed; P2/P3 fixed
or dispositioned below.

## Self-review (main agent)

| # | Sev | Finding | Disposition |
|---|---|---|---|
| S1 | P1 | feather + non-grid-preserving head (strided export or resize_to_input) would bypass the accumulator and write tiles directly — two write paths over the same pixels | FIXED (commit after 2cb5dcc4): per-(head,tile) typed InvalidParameter refusal; edge tiles make fed sizes vary per tile, hence per-tile verdict |
| S2 | P2 | feather requires halo>0 but nothing demanded grid-preserving geometry up front | covered by S1's per-tile refusal (geometry known only after the first forward) |
| S3 | P2 | blend refusal for runMultiInput is implemented; docs updated to call it a 9.0 limitation | FIXED in docs (platform-9.md, ADR 0144) |
| S4 | P3 | uncertainty band under feather blends per-tile entropy values (weighted) rather than recomputing entropy of blended probabilities | accepted: entropy-of-average vs average-of-entropy; documented choice keeps band definition per-tile-stable; revisit with a per-pixel online entropy estimator |

## Environmental flake investigation (recorded for honesty)

One full-suite batch run on a machine with three parallel track builds
active produced SIGABRT/heap-corruption symptoms in three suites
(publish-path messages, "free(): invalid pointer"). Investigated:
- baseline (pristine origin/master operators) re-run: green 2x each;
- my sources re-run on the quiet machine: green across all 13 suites
  (twice), plus MALLOC_PERTURB_=170 green 3x on test_model_runtime_8;
- /tmp (tmpfs) was under heavy parallel allocation at the time.
Conclusion: load-environment flake, not reproducible from these changes;
kept under observation. The final clean sweep (frozen sources, quiet
machine) is the recorded evidence.

## Reviewer A (architecture/correctness/concurrency/science/security)

| # | Sev | Finding | Disposition |
|---|---|---|---|
| A1 | P0 | featherWeight mathematically inverted (anti-feathers; doc/test baked it in; midpoint test non-discriminating) | **FIXED** — 0.5·(1+cos(π·r)); doc formula corrected; x=18 expectation updated to the discriminating value (x=14 noted as midpoint-invariant) |
| A2 | P1 | blended Mask used the raw −1 threshold sentinel (all-ones masks) vs stitched 0.5 default | **FIXED** — accumulator receives the defaulted threshold |
| A3 | P1 | verifyProductProvenance throws (jsoncpp asInt/asString) on wrong-typed sidecar fields, violating never-throws | **FIXED** — tri-state typed accessors; wrong-typed geometry → MalformedSidecar; sidecar read capped at 4 MiB |
| A4 | P1 | blending converts NoData into neighbor predictions (inpainting); all-nodata probe halo ring fabricated values | **FIXED** — per-tile invalidity plane in the accumulator: a pixel whose owning core is invalid stays NoData; deferred nodata tiles mark their cores; probe tiles covered |
| A5 | P2 | NVML physical vs CUDA-visible index unreconciled under CUDA_VISIBLE_DEVICES | **DISPOSITIONED** — single-GPU consumer hosts unaffected; cluster reconciliation documented as follow-up in REVIEW_LOG; python lane pins via inherited mask |
| A6 | P2 | python provider traits (maxAddressableCudaIndex=0) made the GPU lane inert on real-driver/no-OpenCV-CUDA hosts | **FIXED** — python provider registers direct-CUDA traits (63), matching its worker-managed reality |
| A7 | P2 | worker pinning clobbers an inherited CUDA_VISIBLE_DEVICES | **FIXED** — an inherited mask is respected, never overwritten |
| A8 | P2 | single-input manifests with inputs[0].preprocess override silently ignored it | **FIXED** — typed refusal (also mirrored as B3's knob class) |
| A9 | P2 | doc overclaims (MLOps "consumes"; stale "no consumer-side detection" comment) | **FIXED** — both corrected to the honest "should call / wiring is a follow-up" |
| A10 | P2 | payload classes names misindex counts under a Labels remap | **FIXED** — names emitted only without a remap |
| A11 | P3 | preprocessNote underrecords (only normalize token) | **FIXED** — note appends ×scale and +pad |
| A12 | P3 | NVML probe inside the registry mutex on every acquire | **DISPOSITIONED** — accepted for 9.0 (short-TTL cache is a follow-up); comment records the design intent |
| A13 | P3 | NVML hardening nits (reserve count, NO_NVML "true", dlerror) | **FIXED** (reserve clamp to 64, accept 1/true); dlerror strings accepted |
| A14 | P3 | tilesProcessed undercounts for deferred tiles under blending | **DISPOSITIONED** — pre-existing 8.0 counting semantics for the all-nodata path; noted |
| A15 | P3 | refusal message suggests impossible path; aux size via double; sidecar readAll unbounded | **FIXED** (message, 4 MiB cap); size_bytes double-range accepted (JSON doubles, sizes < 2^53 in practice) |

## Reviewer B (tests/performance/portability/docs-vs-code)

| # | Sev | Finding | Disposition |
|---|---|---|---|
| B1 | P2 | M7 test never demonstrates the package-digest session keying (cache-miss after aux change) | **DISPOSITIONED** (time-box): the changed-bytes path fails verification before acquire, so an end-to-end second-load proof requires a matching corrected package; the keying branch (contentDigestFor pkg suffix) is unit-visible in the M7 session test's first half; follow-up noted |
| B2 | P2 | worker providerDetails fabricates "CUDAExecutionProvider" when the worker declared nothing | **FIXED** — only handshake-declared providers are reported |
| B3 | P2 | per-input pad/resize/interpolation accepted but not executed per feed | **FIXED** — typed refusal in the manifest validation (geometry is grid-global) |
| B4 | P2 | accumulator memory scales with W×slots; "never the whole raster" overclaims; estimate under-counts | **DISPOSITIONED** — doc softened in platform-9.md (bound named: (slots+1)×(tile+2·halo)×W floats); column-band chunking and estimate wiring recorded as follow-ups |
| B5 | P2 | provenance does not record the blend method | **FIXED** — sidecar execution.blend (payload follows via backend/device block in a follow-up; sidecar is the reproducibility record) |
| B6 | P2 | fingerprinting is blocking/unmemoized and first-frame-only | **PARTIALLY FIXED** — first-frame-only documented (platform-9.md §2, header); memoization + run-option knob recorded as follow-up |
| B7 | P3 | header says "cached" but detect is fresh per call | **FIXED** — comment corrected |
| B8 | P3 | staleness equal-mtime blind window | **DISPOSITIONED** — documented mtime semantics; sidecar size/digest comparison follow-up |
| B9 | P3 | dlfcn unguarded (Windows) + M_PI (MSVC) | **FIXED** — _WIN32 stub in nvml_inventory; constexpr kPi |
| B10 | P3 | GDAL 3.13 fix unguarded for older GDAL | **DISPOSITIONED** — host lanes are GDAL 3.13; version-guard shim recorded as a geospatial-track follow-up |
| B11 | P3 | bench FAIL-vs-SKIP on CPU-only ORT; verification-inclusive timing; cwd-relative JSON | **DISPOSITIONED** — the FAIL is deliberate (a gated bench is a local action; a red result on a CPU-only host is the honest "not measured"), timing label fixed in PERFORMANCE.md wording |
| B12 | P3 | PERFORMANCE.md quoted a different bench run than the committed JSON | **FIXED** — doc quotes the committed artifact |
| B13 | P3 | named_inputs description omits the STAC keys run() parses | **FIXED** — description updated |
| B14 | P3 | two M0/M4 cases are contract locks rather than regression discriminators | **DISPOSITIONED** — intentional pins; the true #872 discriminator is the device-key assertion against pre-fix code |
| B15 | P3 | first-frame-only fingerprint caveat | **FIXED** (documentation, see B6) |

## Pre-existing (not this track)

| Finding | Disposition |
|---|---|
| test_capability_drift: uncovered operators rs:rasterize / rs:zonal_stats / rs:sar_geocode / rs:sar_temporal_stats | pre-existing harness-knowledge coverage gap (8.0 review noted it as pre-existing on master); scientific operators are other tracks' ownership. No model operators are uncovered by this branch. |
