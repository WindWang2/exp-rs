# ADR 0125: Temporal Workspace — A DataManager-Owned Collection Record

## Context

Temporal 1.0 (`feat/temporal-rs-analysis`, branch `origin/master` @
1ec5094e) shipped a powerful streaming multi-temporal seam
(collection metadata + path references; per-tile, per-date streaming,
NoData→NaN / QA masking, scientific contracts in `runPreflight`, the same
`SpectralIndices` / `MathUtils` kernels the single-scene operators use —
`docs/temporal/ARCHITECTURE.md` §3–§7). Its deployment gap was explicit:

1.  The collection was a **JSON file on disk** (a lightweight workspace
    sidecar). An `(name + descriptor)` re-registration could produce two
    entries pointing at the same scenes. The discovery path was a file
    dialog → path string → `TemporalCollection::load` in whatever dialog or
    operator side re-built it, with no catalog identity.
2.  **Path-only scene identity.** `TemporalSceneRef` already carried optional
    `(assetId, assetRevision)` alongside `path`, but nothing wired it: a scene
    re-commit (a calibration or QA-mask re-run over the same path) did not
    bump the scene's recorded revision, the workflow cache keyed on the
    descriptor path, and the provenance record carried unresolved paths.
3.  **No project persistence.** The collection lived in a sidecar file not
    owned by the project serializer, so saving, exiting and reopening lost it.
4.  **Agent invisibility.** The four `temporal:*` agent tools
    (`create_collection` / `describe_collection` / `list_scenes` /
    `preflight_collection`) were callable but hidden from `tools/list`
    discovery (`mcp_server.cpp` allow-list) and from the Copilot LLM
    (`spatial_tool_provider.cpp:26` dropped every non-`spatial:` tool). `pi/
    exp-rs-spatial.ts` default category `meta,spatial,data` never bridged
    temporal, and `WorkspaceSnapshot` exposed no temporal collections at all.
5.  **Execution-cache path binding.** The revision-aware execution cache
    existed as a contract (`ExecutionResultCache`, `makeExecutionFingerprintV2`
    over `TaggedDerivationInput{assetId @ revision}`) but had zero production
    callers of its `storeOutputPath` wire; the `clear()` regression (#720)
    kept stale path entries. Input-lineage capture had regressed from a full
    catalog scan to an `"input"`-keyed scan (`findInputPathsInParams` →
    dNBR `postfire`, fusion `pan`/`ms` lost) and a plain `absoluteFilePath`
    compare (symlinked / hard-linked / case-variant inputs lost).
6.  **No STAC or remote handle.** STAC existed as an app brownfield
    (`src/app/stac_client.*` + `StacBrowserDialog`), but temporal ingestion
    had zero wire: a STAC `href` would hit `QFile::exists` in
    `collectionFromInput` and fail with "scene not found", a remote
    `SourceDescriptor` was mis-classified as `Missing` by a `QFileInfo` probe,
    and `absolutePathOutsideWorkspace` wrongly allowed `https://` and wrongly
    rejected `/vsicurl/` depending on platform.
7.  **No workspace UI affordance.** `DataManagerPanel` rendered
    collections + assets; there was no temporal group, no action, no revision.
8.  **Several undetected compiler regressions** from the squash-merge of
    #711 (back into #708/#709/: pool-size revert inside #661→#713, streaming
    denials into honest `FullRaster` (#716), infinite-NoData branch as a no-op,
    `markTaskFailed` without its own cancel, `clear` leaving stale path entries,
    layout designer 4-tu);

A unification that concepts to fix the four left behind by #708 alone.

## Decision

### 1. A DataManager-owned `TemporalCollectionRecord`, not a new `AssetKind`

A new `AssetKind` would chass of the exhaustive kind switches over
`workspace_snapshot.cpp:30-41`, `structuresCompatible` (`data_asset.h:217`),
the serializer's kind-specific blocks, `AssetQuery` and the `AssetStructure`
closed variant — with no new rendering capability added. A collection is
**catalog organizational** (ordered scenes sharing a temporal extent),
not a renderable dataset. Reuse the `CollectionSnapshot`-shape read-only
record, not a renderable kind. Its new data-layer type
`TemporalCollectionRecord` (`src/data/temporal_workspace_types.h`) carries
`CollectionId id`, `QString displayName`, `QString descriptor`,
`quint64 revision`, `QDateTime createdAtUtc/updatedAtUtc`. The `descriptor`
is the canonical JSON document owned by the temporal layer
(`exp_rs_temporal_collection/1`, `TemporalCollection::toJson` → text), stored
verbatim as JSON text so `sicnu_data` stays free of any `sicnu_processing`
dependency. The temporal seam `temporal_workspace.h/.cpp` owns scene→asset
binding (`bindCollectionAssets`, per-scene `assetId` + `assetRevision` on the
ref before serializing) and the conversion (`collectionDescriptorText` /
`collectionFromDescriptorText`). Consumers that must detect a scene-content
change resolve the scene's **current** `AssetRevision` live at use time
(`findByPath` → `snapshot->revision()`), never the stored snapshot.

Project persistence: a fourth XML block `<temporalCollections>(id, revision,
name, <descriptor>)` in `src/app/data_project_serializer.cpp`, written after
`<virtualRasters>` and read with `restoreTemporalCollection(id, revision,
request)` after the scene assets are restored (so bindings re-resolve lazily).

### 2. Operator `collection = <workspace record id or file path>`

`rs_temporal_collection_input.cpp:parseCollection` treats `collection`
that parses as a `QUuid` as a workspace record id: when
`workspaceCatalog()` is set, it reads the record's full descriptor (the typed
collection with per-scene bindings); a plain path falls back to
`TemporalCollection::load`. The wiring seam is a global, process-wide
`temporal_workspace.h:workspaceCatalog()` pointer set once at startup by each
host (`main.cpp` MCP `DataManager`, `main_window.cpp` `ProjectContext`
DataManager, `rs_pipeline_runner.cpp`), on the owning thread.

### 3. Revision-aware execution cache, OFF by default

Fingerprint: `execution_fingerprint.h:makeExecutionFingerprintV2(alg, ver,
RFC-8785 params, TaggedDerivationInput{assetId @ revision}…)` — the cache
hashes identity + revision, never file path. Temporal adds the seam
`fingerprintInputsForOperatorParams` that collects:

* generic string/list path params that resolve to registered local rasters
  (`findInputPathsInParams(exclude={outputPath})` + canonical path
  resolution);
* inline scene entries (`scenes: [path | {path}]`) against the catalog;
* the `collection` parameter through `fingerprintInputsForCollectionParam`
  (workspace record id → collection-level input plus every scene's live
  asset revision, or the descriptor file iff identical to a workspace record;
  otherwise uncacheable);

so a cached output reuses the **current** scene content hash, not the
stored-input snapshot. Any unresolvable local input or path-only scene yields
an invalid fingerprint — the conservative uncacheable verdict.

Dispatch: `TaskCenter::taskExecutionFingerprintLocked` (post-placeholder,
pre-admit, on the catalog thread; gated on `isEnabled()`, on a catalog being
wired, on the operator being deterministic), `m_taskFingerprints[taskId]`
dispatch-map, `flushPendingLaunches` outside `m_mutex`
(file copy + `markTaskCompleted`), `storePipelineStepOutputLocked`
on a real completion (so only revision-identified runs seed the cache),
`clearCompletedTasks` + terminal (`Failed`/`Canceled`) pruning, plus #720
regression fixes (`clear()` also clears `m_pathEntries`, failure also cancels
its engine job).

### 4. STAC ingestion: a pure adapter over a saved search document

`src/processing/algorithms/temporal/temporal_stac_adapter.h/.cpp` (`StacItem`,
`parseStacItem` — raster-asset selection + mandatory `datetime`, `platform`,
processing-level, `eo:cloud_cover`, geometry + property surface —,
`filterStacItems` — bbox + datetime range + `property_filter=key=value` (string
insensitive or numeric equality) + limit, chronologically sorted —,
`temporalCollectionFromStacItems*`). Client-side filtering covers online and
offline consumers — the same code filters a synthetic local fixture as it does a
real response. `temporal_workspace_tools.h/.cpp:temporal:ingest_stac` exposes
`bbox / datetime / property_filter / limit` filtering over a `result` file path
or an inline `result_json` and an optional `register` flag (remote
`/vsicurl/`-prefixed hrefs stay valid).

### 5. Remote raster policy: behind `GdalRasterSourceProvider`

`src/data/providers/gdal_raster_source_provider.cpp` now recognizes remote
canonicals (`http(s)://` → `/vsicurl/`-prefixed, already-prefixed
`/vsicurl/`), derives identity/extension/`StorageKind::Remote`, skips the
`QFileInfo` existence probe (remote sources gate on a GDAL open under
`GDAL_HTTP{,_CONNECT}_TIMEOUT=30/10`, `_MAX_RETRY=3`, `_VERSION=2` defaults)
and reports `Missing` vs `Error` by remote vs local semantics;
`McpServer::absolutePathOutsideWorkspace` treats URL / VSI-virtual-path
centrally (remote refs are read-only data inputs that fix their scheme,
`file://` maps to its local path); `temporal_collection_tools`
accepts remote scene hrefs without a local existence probe.

### 6. Agent / Pi: discovery by default

* New workspace-typed tools `temporal:list_collections`,
  `temporal:get_collection`, `temporal:register_collection`,
  `temporal:remove_collection`, and `temporal:ingest_stac`
  (`temporal_workspace_tools.h/.cpp`).
* `McpServer::kAllowed` allow-list now includes `temporal:` so the
  `temporal:*` tools appear in `tools/list` discovery.
* `SpatialToolProvider::provideTools` now exposes `temporal:*` alongside
  `spatial:*` to the Copilot LLM (it previously dropped every `temporal:`
  tool).
* `pi/exp-rs-spatial.ts` default `EXP_RS_TOOL_CATEGORIES`
  `meta,spatial,data` → `meta,spatial,data,temporal` so Pi's
  `wantedCategories` always bridges them (still overridable).
* `Agent::WorkspaceSnapshot` now carries `TemporalCollectionInfo` per
  workspace record (id, name, revision, scene count, bound-scene ratio,
  time range, platforms) into the Copilot LLM's system prompt.

### 7. Panel: a temporal group with Describe/Preflight/Analyze/Remove

`src/app/panels/data_manager_panel.cpp` now renders a violet-striped
"Temporal Collections" group (count, per-record id, name, revision) above the
asset tree, with a per-record context menu (Describe → info summary,
Preflight → `runPreflight` report, Open Temporal Analysis → analyze,
Remove → `removeTemporalCollection`) — no separate temporal manager window.

### 8. Building blocks restored

Undo-stack guard in `applyItemPropertyMutators` (#717), NCHW pack for
inference (#671), streaming regression fixes (`majority_filter` NoData,
spectral-index honest declarations, infinite-NoData sweep), catalog scan +
affinity + filesystem path, MSVC build fixes for `band_math_*` stabs.

## Consequences

The temporal collection's identity is no longer "a file path + an SC
range", it is a DataManager record that can be listed, described, removed,
persisted, versioned, and revision-compared. STAC items become a collection
without touching the network. Agents can discover, get, preprocess and run a
temporal collection without the user pasting a descriptor path. The execution
cache can now tell an `InputFileChanged`-vs-`Reparable` cached perspective via a
current-asset revision instead of a stale `path`, and its own store is
cache-hit resilient. Existing consumers that address a collection by file
still work: a plain descriptor path is still loadable.

## Alternatives Considered

* New `AssetKind::TemporalCollection` — rejected: every exhaustive switch and
  every kind-inspecting serializer block would have to learn it despite having
  nothing to render or write; the dedicated `TemporalCollectionRecord` path
  keeps the data layer tight and reuses the existing project-serializer block
  pattern like `<virtualRasters>`.
* Reimplementing the STAC client inside `src/data` — rejected: STAC lives in
  `src/app` where `QNetworkAccessManager` belongs; temporal ingestion consumes
  an already-fetched response document, never an endpoint.
