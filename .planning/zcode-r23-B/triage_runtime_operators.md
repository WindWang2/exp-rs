# r23-B Triage: runtime/operators issues vs HEAD ad98a7a704

Audit date: 2026-10-27. Worktree: `/home/kevin/projects/rs-studio/exp-rs-r23-B-20261026T164130Z` (read-only).
Issue bodies: `.planning/zcode-r23-B/open_issues_raw.json`. B-track whitelist: `src/operators/runtime/**`, `src/runtime/**`, `src/operators/framework/model_catalog*` only.

## Summary table

| Issue | Verdict | B-track scope | File(s) | One-line evidence |
|---|---|---|---|---|
| #1541 | FIXED_OPEN (9b42bc93f7) | IN | src/operators/runtime/detection_tile_engine.cpp:331 | `if ( bandCount <= 0 \|\| bandCount > CV_CN_MAX )` typed refusal now present |
| #1553 | FIXED_OPEN (d9f1b886f1) | IN | src/runtime/chunk/chunk_graph.cpp:58,100 | `requireFreeConsumerLocked` rejects `input == m_sinkInput`; `addSink` calls the guard |
| #1554 | FIXED_OPEN (d9f1b886f1) | IN | src/runtime/gpu/gpu_plane.cpp:44-64 | dtor locks `m_impl->mutex` in a scoped block, then `delete m_impl; m_impl = nullptr;` |
| #1575 | FIXED_OPEN (9b42bc93f7) | IN | src/operators/runtime/tile_inference_engine.cpp:4279-4287 | per-sample estimate now uses `tile + 2*halo` and `max(..., model.input.width/height)` for `to_input` |
| #1576 | PARTIAL | IN | src/operators/runtime/openvino_provider.cpp:60 vs :271 | fix (private `selectDevice`) landed in 9b42 then its definition was dropped by merge 9988aa15a9; line 60 now calls an **undefined** `selectDevice` → OV-enabled builds do not compile; `ProviderTraits{}` registration + rationale (:255-271) survived |
| #1577 | PARTIAL | IN | tensorrt_provider.cpp:148 / openvino_provider.cpp:107 | placeholders gone (real path stored), but TRT `artifactPath()` returns non-existent member `m_artifactPath` (compile break when TRT enabled); OV half fixed but same TU broken by the dangling `selectDevice` call |
| #1578 | FIXED_OPEN — OUT_OF_SCOPE for B (src/operators/otb) | OUT | src/operators/otb/otb_segmentation_operator.cpp:43-67,180-202 | `boundedInt`/`boundedDouble` throw typed `InvalidParameter` for all 8 `setRange` params |
| #1603 | FIXED_OPEN (d9f1b886f1) | IN | src/runtime/exec/execution_governor.cpp:46,583-607 | 30 s `kRssThrottleDeadline` + `maxPolls` backstop + proceed-under-pressure degrade + telemetry |
| #1604 | FIXED_OPEN (d9f1b886f1) | IN | src/runtime/observability/trace.cpp:267-270,354-383 | persistent `thread_local std::ofstream`, open-once, post-write state check with recovery, idle flush |
| #1605 | FIXED_OPEN (d9f1b886f1) | IN | src/runtime/observability/execution_telemetry.cpp:103-118 | ring-full path overwrites `m_events[cursor]` in place — O(1), no `erase` |
| #1642 | FIXED_OPEN (9b42bc93f7), scope split | sub2 IN; sub1/3/4 OUT | see sub-item table below | all 4 sub-items fixed in code; only sub-item 2 lives in a B-whitelist dir |

## Meta-finding: r21-B (PR #1673) fix claims were misattributed

The real production fixes for these issues are two sweep commits authored 2026-10-19 17:28 (+0800):

- `9b42bc93f7` — "fix(src/operators): resolve full-review findings #1541 #1542 #1543 #1544 #1575 #1576 #1577 #1578 #1642" (operators tree: registry, io, otb, python, rs, runtime engines/providers)
- `d9f1b886f1` — "fix(src/runtime): resolve full-review findings #1553 #1554 #1603 #1604 #1605" (chunk_graph, gpu_plane, execution_governor, trace, execution_telemetry)

Both entered master via `fix/full-review-sweep` (merges 9988aa15a9 / e12a9afbc8 = PR #1662). PR #1673's only operators-tree change (388f7db02f) to `tile_inference_engine.cpp` was mask_threshold role gating (risk B5), **not** #1575 — the #1575 fix predates it in 9b42bc93f7. The issues can be closed on GitHub for everything except #1576/#1577's provider-file state (below).

## Meta-finding 2: merge 9988aa15a9 re-broke both optional providers

`9988aa15a9` ("Merge remote-tracking branch 'origin/master' into fix/full-review-sweep") unioned the sweep branch's provider files against the master side, which had gone through 5933f70421 (PR #1659, r18-b) where the R15-C version of those files (no `selectDevice`, member `mArtifact`) had won. The hand-union kept 9b42's call sites but dropped the new definitions/members:

- `openvino_provider.cpp`: call `selectDevice( model, hw, error )` (line 60) survived; the private `std::string selectDevice(...)` member function and its helpers (`manifestPinsCpu`, `joinDevices`) were dropped. Repo-wide grep: `selectDevice` occurs only at lines 60/62/267 (a call, a comment, a comment).
- `tensorrt_provider.cpp`: `artifactPath()` (line 148) still returns `m_artifactPath` (9b42's member name), but the private member was unioned back to `mArtifact` (line 335). Repo-wide grep: `m_artifactPath` occurs only at line 148 — undeclared identifier.

Both TUs are compiled only when the optional providers are enabled (`src/operators/CMakeLists.txt:311` `option(SICNU_ENABLE_TENSORRT ... OFF)`, `:327` `option(SICNU_ENABLE_OPENVINO ... OFF)`; no preset or .cmd enables them), so default builds are unaffected — the entire classes sit inside `#ifdef SICNU_WITH_TENSORRT` / `#ifdef SICNU_WITH_OPENVINO` and degrade to no-op registration stubs. Any build with either provider enabled fails to compile today.

## Per-issue evidence

### #1541 — detection lane CV_CN_MAX guard — FIXED_OPEN (commit 9b42bc93f7)

`src/operators/runtime/detection_tile_engine.cpp:323-336`:

```cpp
  const int bandCount = static_cast<int>( bandList.size() );
  // #1541: CV_MAKETYPE packs the channel count into the type bits modulo
  // CV_CN_MAX — a >CV_CN_MAX-band selection (e.g. a hyperspectral raster
  // with bands unset) wraps CV_32FC(bandCount), ...
  if ( bandCount <= 0 || bandCount > CV_CN_MAX )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "input selection carries " + std::to_string( bandCount )
                             + " bands but cv::Mat supports at most " + std::to_string( CV_CN_MAX )
                             + " channels — declare input.bands to select a subset" );
```

Exactly the fix direction the issue requested (typed refusal mirroring the raster engine). Fix commit: `git log -1 -S'input selection carries' -- src/operators/runtime/detection_tile_engine.cpp` → `9b42bc93f7 fix(src/operators): resolve full-review findings #1541 #1542 #1543 #1544 #1575 #1576 #1577 #1578 #1642`.

### #1553 — addSink bypasses single-consumer guard — FIXED_OPEN (commit d9f1b886f1)

`src/runtime/chunk/chunk_graph.cpp:44-59` (guard now sees the pending sink edge):

```cpp
    // The sink node is materialized only in run(), so its pending input
    // occupies the single-consumer slot from addSink onward: without this
    // check addSink(x) followed by addStage(x)/addJoin({x}) would still fan
    // the queue out to two poppers (F-A-10).
    if ( input == m_sinkInput )
        rejectFanOut();
```

`src/runtime/chunk/chunk_graph.cpp:92-103` (addSink itself guards, same-input replacement stays legal):

```cpp
    // Same single-consumer guard as addStage/addJoin (F-A-10): the sink edge
    // counts as a consumer from build time, in either build order. ...
    if ( input != m_sinkInput )
        requireFreeConsumerLocked( input );
    m_sinkInput = input;
    m_sink = std::move( fn );
```

Fix commit: `git log -1 -S'occupies the single-consumer slot from addSink' -- src/runtime/chunk/chunk_graph.cpp` → `d9f1b886f1 fix(src/runtime): resolve full-review findings #1553 #1554 #1603 #1604 #1605`.

### #1554 — ModelSessionPool dtor unguarded — FIXED_OPEN (commit d9f1b886f1)

`src/runtime/gpu/gpu_plane.cpp:44-64`:

```cpp
ModelSessionPool::~ModelSessionPool()
{
    if ( !m_impl )
        return;
    // Same lock discipline as every other method that touches sessions ...
    {
        std::lock_guard<std::mutex> lock( m_impl->mutex );
        if ( m_impl->backend )
        {
            for ( const auto &live : m_impl->sessions )
                m_impl->backend->freeVram( live.session->deviceId, live.vramMb );
        }
    }
    delete m_impl;
    m_impl = nullptr;
}
```

Lock scope deliberately ends before `delete` (unlocking inside a destroyed Impl would be UB) — exactly the issue's suggested fix. Fix commit marker: `git log -1 -S'm_impl = nullptr' -- src/runtime/gpu/gpu_plane.cpp` → `d9f1b886f1`.

### #1575 — effectiveBatchSize ignores halo / to_input — FIXED_OPEN (commit 9b42bc93f7)

`src/operators/runtime/tile_inference_engine.cpp:4270-4287` (inside `TileInferenceEngine::effectiveBatchSize`, :4253):

```cpp
  const int tile = std::max( kMinTileSize, tilePx );
  const int halo = std::max( 0, effectiveHalo( model ) );
  // #1575: the per-sample blob is the FED window — tile + 2*halo per side,
  // not the bare core tile — and preprocess.resize="to_input" replaces that
  // geometry with the fixed graph input ... Estimate on the LARGER of the two
  // geometries so neither the halo window nor the fed tensor is under-counted
  long long fedW = static_cast<long long>( tile ) + 2 * halo;
  long long fedH = fedW;
  if ( model.preprocess.resize == "to_input"
       && model.input.width > 0 && model.input.height > 0 )
  {
    fedW = std::max<long long>( fedW, model.input.width );
    fedH = std::max<long long>( fedH, model.input.height );
  }
  const unsigned long long perSampleBytes =
    4ULL * static_cast<unsigned long long>( fedW ) * static_cast<unsigned long long>( fedH )
      * static_cast<unsigned long long>( std::max( 1, fedChannels ) ) * 4ULL;
```

Both callers benefit without signature change (the function reads `model` for halo/input dims): raster engine at tile_inference_engine.cpp:1271/:3500, detection engine at detection_tile_engine.cpp:359-360. Fix commit: `git log -1 -S'#1575: the per-sample blob is the FED window'` → `9b42bc93f7`.

### #1576 — OpenVINO ProviderTraits / CUDA-gated device choice — PARTIAL

What survived at HEAD (`src/operators/runtime/openvino_provider.cpp:255-271`):

```cpp
  // ProviderTraits{} (maxAddressableCudaIndex 0): OpenVINO addresses NO
  // CUDA device — its "GPU" token is the Intel plugin ... The registry's
  // verdict therefore stays authoritative; the provider only supplies the
  // one repair that resolution cannot make (selectDevice above): ...
  registry.registerProvider( "openvino", makeOpenVinoRuntime, ProviderTraits{} );
```

What was lost: 9b42bc93f7 added a private member `std::string selectDevice( const ModelInfo&, const ModelHardwareCapabilities&, std::string* )` implementing the #1576 repair (try the Intel GPU plugin when the registry demoted a GPU-wanting manifest to CPU purely for CUDA absence; typed refusal when a booked GPU can't be served). At HEAD, `load()` still calls it —

`openvino_provider.cpp:60`:
```cpp
        const std::string device = selectDevice( model, hw, error );
```

— but the definition does not exist anywhere (repo-wide grep: only lines 60/62/267, i.e. one call + two comments). First broken snapshot: merge `9988aa15a9` (fix/full-review-sweep taking master's R15-C file via 5933f70421/PR #1659 and re-adding the sweep's call site). Consequence: `SICNU_WITH_OPENVINO=1` builds fail to compile; default builds (option OFF, no preset enables it — `src/operators/CMakeLists.txt:327`) compile the no-op stub, so the original silent-CPU-demotion symptom cannot manifest in shipped default builds, and neither can OpenVINO.

Remaining gap (precise): restore the `selectDevice` member function + `manifestPinsCpu`/`joinDevices` helpers from commit 9b42bc93f7 into `OpenVinoRuntime` (one contiguous block, `git show 9b42bc93f7:src/operators/runtime/openvino_provider.cpp`). Production path affected: `ModelRuntimeRegistry` ctor → `registerOpenVinoProvider` (model_runtime.cpp:786) → `makeOpenVinoRuntime` → `OpenVinoRuntime::load` for every `framework: openvino` manifest — currently only reachable in provider-enabled builds, which are unbuildable.

### #1577 — TRT/OV artifactPath() hardcoded — PARTIAL

Placeholders are gone in both providers:

`tensorrt_provider.cpp:84-85` + `:335`:
```cpp
    TensorRtRuntime( const std::string &enginePath, int cudaIndex )
      : mArtifact( enginePath ), m_cudaIndex( cudaIndex ) {}
    ...
    std::string mArtifact; ///< the engine file this session loaded (R15-C C05/B8:
                           ///< was the hardcoded placeholder "tensorrt-engine", ...
```

`openvino_provider.cpp:69-70` + `:107`:
```cpp
        mArtifact = path;              // path = model.resolvedArtifactPath (:46)
    ...
    std::string artifactPath() const override { return mArtifact; }
```

Remaining gap 1 (TensorRT): the accessor was unioned against the renamed member —

`tensorrt_provider.cpp:148`:
```cpp
    std::string artifactPath() const override { return m_artifactPath; }
```

`m_artifactPath` is declared nowhere (repo-wide grep: this line only) — 9b42 had renamed the member to `m_artifactPath`, merge 9988aa15a9 restored the member as `mArtifact` but kept the new accessor. `SICNU_WITH_TENSORRT=1` builds fail to compile. Minimal fix: change line 148 to `return mArtifact;`.

Remaining gap 2 (OpenVINO): `artifactPath()` itself is correct, but the same TU contains the dangling `selectDevice` call (#1576) — the file cannot compile with `SICNU_WITH_OPENVINO=1` either.

Severity note: `IModelRuntime::artifactPath()` (model_runtime.h:259-260) currently has no in-tree production call sites (grep for `->artifactPath()` / `.artifactPath()` in src/ and tests/: zero hits on sessions), so the contract pollution is latent for external consumers only; the compile break is the concrete defect. Fix commits: 9b42bc93f7 (semantic fix) then regressed by merge 9988aa15a9.

### #1578 — setRange is metadata-only in otb segmentation — FIXED_OPEN, but OUT_OF_SCOPE for B-track

`src/operators/otb/otb_segmentation_operator.cpp:43-53`:

```cpp
int boundedInt(const Json::Value& params, const std::string& key, int defaultValue,
               int minValue, int maxValue) {
    const int value = getInt(params, key, defaultValue);
    if (value < minValue || value > maxValue) {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            rangeMessage(key, ...));
    }
    return value;
}
```

All 8 declared ranges are now enforced at the seam (:180-202): `boundedInt(params, "spatialRadius", 5, 1, 1000)`, `boundedDouble(params, "rangeRadius", 15.0, 0.1, 1000.0)`, `minRegionSize 1..100000`, `maxIterations 1..10000`, `threshold 0.0001..1.0` (both filters), `profileSize/startRadius/radiusStep 1..100`, `sigma 0.1..100.0`. Fix commit: `git log -1 -S'boundedInt' -- src/operators/otb/otb_segmentation_operator.cpp` → `9b42bc93f7`. Scope: `src/operators/otb` is another track's whitelist; noting for cross-track closure only. (Caveat: the generic `rs_json_params.h` still has no bounded getters — the fix is local to this operator; other operators' 72 `setRange` declarations repo-wide remain metadata-only, but that wider population is outside this issue's file.)

### #1603 — throttleWait unbounded RSS loop — FIXED_OPEN (commit d9f1b886f1)

`src/runtime/exec/execution_governor.cpp:46` + `:581-607`:

```cpp
constexpr std::chrono::milliseconds kRssThrottleDeadline{ 30 * 1000 };
...
    const auto deadline = t0 + kRssThrottleDeadline;
    const std::uint64_t maxPolls =
        static_cast<std::uint64_t>( kRssThrottleDeadline / pollInterval ) + 1;
    ...
        if ( ++polls >= maxPolls || std::chrono::steady_clock::now() >= deadline )
        {
            // Watermark never cleared and no cancel predicate can rescue us:
            // degrade to proceed-under-pressure instead of parking forever.
            deadlineExceeded = true;
            break;
        }
```

Both escapes the issue demanded: total deadline (30 s) and an iteration backstop, with a distinct telemetry subject `exec.governor.rss_throttle_deadline` for proceed-under-pressure events. The empty-cancel-flag caller path (chunk_pipeline's default-null `m_cancelFlag`) is now bounded by the deadline regardless. Fix commit: `git log -1 -S'kRssThrottleDeadline'` → `d9f1b886f1`.

### #1604 — FileTraceSink reopens ofstream per record — FIXED_OPEN (commit d9f1b886f1)

`src/runtime/observability/trace.cpp:267-270` + writer loop `:354-383`:

```cpp
std::ofstream &writerStream()
{
    static thread_local std::ofstream stream;   // handle kept across records
...
            std::ofstream &file = writerStream();
            rotateIfNeeded_locked();
            openFile_locked();
            if ( !m_activePath.empty() )
            {
                if ( !file.is_open() )
                {
                    file.clear(); // drop any failbit left by a previous failure
                    file.open( ..., std::ios::app | std::ios::binary );
                }
                if ( file ) { file << line << "\n"; m_activeBytes += line.size() + 1; }
                if ( !file )  // open OR write failed: release + clear, retry next record
                { if ( file.is_open() ) file.close(); file.clear(); }
            }
            if ( drained && file.is_open() )
                file.flush();
```

Handle reuse (open-once), post-write stream-state validation with recovery (the swallowed-write-failure half of the issue), idle flush, and rotation closes the handle before rename (Windows). Fix commit: `git log -1 -S'writerStream' -- src/runtime/observability/trace.cpp` → `d9f1b886f1`.

### #1605 — event ring erase(begin, begin+size/4) — FIXED_OPEN (commit d9f1b886f1)

`src/runtime/observability/execution_telemetry.cpp:103-118`:

```cpp
    if ( m_events.size() < kEventCapacity )
    {
        m_events.push_back( std::move( bounded ) );
        return;
    }
    // Ring full: overwrite the oldest slot in place. One slot write per record
    // — evicting a quarter of the vector here instead would move O(capacity)
    // events on every post-capacity record, quadratic over a run.
    size_t &cursor = eventRingCursor();
    if ( cursor >= kEventCapacity )
        cursor = 0;
    m_events[cursor] = std::move( bounded );
    ++cursor;
```

O(1) slot overwrite honors the header contract ("one ring slot write", execution_telemetry.h:10-13); the read side unwraps the cursor (`events()` :157-167), `clearEvents()` resets it (invariant documented at :64-68). No `erase` remains in the record path. Fix commit: `git log -1 -S'm_events[cursor] = std::move( bounded )'` → `d9f1b886f1`.

### #1642 — src/operators tech-debt, 4 sub-items — FIXED_OPEN (9b42bc93f7); scope split

| Sub | File | B-whitelist? | Status | Evidence |
|---|---|---|---|---|
| 1. listSchemas() constructs operators under m_mutex | src/operators/framework/rs_operator_registry.cpp:88-109 | **OUT** (framework, non-model_catalog) | FIXED | factories copied out under the lock, `op = pair.second(); op->schema()` built OUTSIDE it — "the discipline create() follows above"; self-deadlock/serialization causes removed |
| 2. const_cast on non-mutable m_inferMutex | src/operators/runtime/opencv_dnn_runtime.h:91, .cpp:214 | **IN** | FIXED | header now `mutable std::mutex m_inferMutex;` (comment at :88 explains); `outputTensorNames() const` takes a plain `std::lock_guard<std::mutex> lock( m_inferMutex )`; `const_cast` gone from the TU (grep: 0 hits) |
| 3. array params declared "type":"string" | src/operators/io/io_operators.cpp:102-115 (+ 11 usages), io_fabric_operators.cpp:86-97 | **OUT** (io) | FIXED (io family) | new `arrayParam(name, desc, items, minItems, maxItems)` helper types `"type":"array"`; `bounds` in io_fabric_operators.cpp is hand-built with `type=array`, `minItems 4`, `maxItems 6`, `items {type:number}`; `levels` (:661), `patches` (:1056), `bands`/`creationOptions` (:265-266) all converted |
| 4. mapFabricCode/fabricGuarded duplicate of mapGeoCode/guarded | src/operators/io/io_fabric_operators.cpp:25, io_operators.cpp:35,74 | **OUT** (io) | FIXED | `mapFabricCode`/`fabricGuarded` deleted (repo-wide grep: 0 hits); io_fabric declares `Json::Value guarded( ... );` and reuses io_operators.cpp's single `guarded`/`mapGeoCode` — one authoritative 21-case mapping |

Fix commit for all four: `git show 9b42bc93f7 --stat` (registry 22+, io_fabric 72+/-, io_operators 67+, opencv_dnn_runtime .h/.cpp 7+); markers: `-S'mutable std::mutex m_inferMutex'` → 9b42bc93f7, `-S'Json::Value arrayParam'` → 9b42bc93f7.

Residual observation (NOT part of #1642's cited files, other tracks): the same string-typed-array-schema pattern persists in `src/operators/rs/` (e.g. rs_kmeans_operator.cpp:108 `bands`, rs_stack_register_operator.cpp:73 `scenes`, rs_temporal_extract_regions_operator.cpp:106 `regions`, rs_post_classification_change_operator.cpp:52 `class_labels`) — a candidate follow-up issue for the rs track.

## Verdict legend

- FIXED_OPEN — code fixed at HEAD while the GitHub issue is still open (fix commit quoted); safe to close with the commit reference.
- PARTIAL — the complained-about defect is gone/incomplete; precise remaining gap stated.
- OUT_OF_SCOPE — file belongs to another track's whitelist; verified anyway for cross-track closure.
