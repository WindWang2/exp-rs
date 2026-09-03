# Temporal Remote Sensing 2.0 — Architecture Plan

Addendum to `docs/temporal/ARCHITECTURE.md` (goal 2026-08-16, ADR 2.0).

Temporal 1.0 shipped streaming multi-temporal operators over a JSON-descriptor
collection (metadata + path references, a lightweight workspace sidecar). Temporal
2.0 makes that collection a first-class workspace asset without reinventing any
of the platform's existing seams.

Goal: **Temporal Workspace** — the agent and the operator (and the project
serializer, the cache, and the provenance record) all address the same
revision-identifiable collection record, not a path list that can be stale,
leak, or evade cache/provenance.

## 1. Core design decisions

### 1.1 `DataManager` owns the record identity, not the typed collection

Adding a new `AssetKind` would ripple through every exhaustive kind switch
(workspace snapshot, structures-compat, kind-specific serializer blocks,
AssetQuery). A temporal collection is a **catalog entity** (an ordered group
of scenes with shared temporal metadata), not a renderable dataset — so it
is modeled as a dedicated record kind with the same shape as the existing
`CollectionSnapshot` + child-asset machinery, not as a new renderable kind.

New data-layer type `TemporalCollectionRecord` (`src/data/temporal_workspace_types.h`):
`id` (`CollectionId`), `displayName`, `descriptor` (the opaque, canonical JSON
document owned by the temporal layer), `revision`, `createdAtUtc`,
`updatedAtUtc`. Stored verbatim as JSON text so `sicnu_data` stays free of any
dependency on `sicnu_processing` — the temporal layer owns the schema
(`exp_rs_temporal_collection/1`) and the `TemporalCollection` ↔ `TemporalSceneRef`
↔ descriptor conversion; the data layer serializes it like it already
serializes `<derivation>` and `<recipe>` payloads.

Why not store the typed `TemporalCollection` directly? The DataManager would
then depend on the temporal core, reversing the host–client direction
(DataManager → temporal). Keeping the document opaque preserves the layering:
`temporal_workspace.h/.cpp` binds scenes to registered Data Assets and converts,
`DataManager` stores.

### 1.2 Scene references: per-scene `(assetId, revision)` **also** written into the descriptor

`src/processing/algorithms/temporal/temporal_collection.h`: `TemporalSceneRef`
already carries `assetId`/`assetRevision` as `QString`s alongside `path`. The
workspace seam (`temporal_workspace.h:bindCollectionAssets`) resolves every
`path` against the `DataManager` and stamps the live asset identity onto the
field before serializing the descriptor into the record. The descriptor is then
`save`d/`load`ed as the canonical form — so the path is fallback/diagnostics
while the `(assetId, revision)` is the real data identity. That is exactly what
lets the fingerprint and provenance depend on asset identity, not file name.

### 1.3 Project persistence is a serializer block, not a temporal database

The existing `<sicnuDataManager>` extension element (`src/app/data_project_serializer.cpp`)
writes three blocks (`<assets>`, `<collections>`, `<virtualRasters>`);
temporal adds a fourth `<temporalCollections>` with `(id, revision, name,
<descriptor>)` per record. `write()` writes the block after `<virtualRasters>`,
`read()` restores it with `restoreTemporalCollection(id, revision, request)` after
the scene assets are restored — so `bindCollectionAssets` re-binds lazily at
load time. A missing `<temporalCollections>` (projects saved before temporal
2.0) simply has no records: additive, not a migration.

## 2. Operator seam: `collection = <record id or path>`

`src/operators/rs/rs_temporal_collection_input.h/.cpp` (`parseCollection`):
a `collection` parameter that parses as a `QUuid` now addresses a
`TemporalCollection` registered in the workspace (`workspaceCatalog()` is set
once at startup by each host — GUI `main_window.cpp`, MCP `main.cpp`, CLI
`rs_pipeline_runner.cpp` — on the owning thread, matching TaskCenter's catalog
affinity rule). The workload reads the DataManager record's full descriptor
(the typed collection, including its per-scene `assetId`/`assetRevision`);
a file-system descriptor path falls back to `TemporalCollection::load`. A UUID
that does not resolve is a hard error — never a silent reinterpretation as a
relative path.

## 3. Execution cache identity: revision-live, never path-bound

Fingerprint `src/data/execution_fingerprint.h` (`makeExecutionFingerprintV2`
over `TaggedDerivationInput{assetId, revision, fromPort, toPort, bandRefs,
valueDomain, lazyContentDigest}`): the contract already says "asset identity +
revision, never file path". Temporal extends it with a temporal-specific seam
`temporal_workspace.fingerprintInputsForOperatorParams` that builds that list
for an operator task's parameters:

* every generic string/list path parameter in the task's `parameterMap` that
  resolves to a registered local raster (local existence probe) —
  `findInputPathsInParams(exclude={outputPath})` + `findByPath` canonical
  resolution (fixed to `canonicalFilePath`, #718);
* every inline scene entry in `"scenes"` (both bare `path` strings and
  `{"path": "..."}` objects) against the catalog;
* the `collection` parameter (workspace record id → collection-level input plus
  every scene's **current** asset revision resolved live, not the stored
  `assetRevision` snapshot);

Any failure — an unregistered local path, a path-only scene, a workspace
collection with an unbound scene, a UUID that does not resolve — returns
an invalid fingerprint: the conservative uncacheable verdict that keeps hits
honest (a hit that cannot prove input identity was wrong would corrupt output).

TaskCenter dispatch then carries the fingerprint from staging
(`taskExecutionFingerprintLocked`, post-placeholder, pre-admit) through a
`m_taskFingerprints[taskId]` map, serves via
`ExecutionResultCache::storeOutputPath` / `lookupOutputPath` in
`flushPendingLaunches` (outside `m_mutex`, so the file copy + the
`markTaskCompleted` transition do not hold the lock), stores on a real
completion (so only revision-identified executions ever seed the cache),
and prunes on every terminal status / on `clearCompletedTasks`. The cache is
`OFF` by default; deterministic temporal operators are `deterministic == true`
in their `metadata()`, so they are the first family that opts in. All
consumers must now fail closed consistently: #720's fix.

## 4. STAC ingestion: a pure adapter over a saved search document

Goal (§8): a minimal, offline-testable seam `STAC search → items →
TemporalCollection` that does not copy a full STAC schema.

`src/processing/algorithms/temporal/temporal_stac_adapter.h/.cpp`:
`StacItem` (id + datetime + platform + eo:cloud_cover + a selected raster
`href` + its asset key + its eo:bands), `parseStacItem` (validates raster-asset
selection + mandatory `datetime`, extracts platform/processing-level/cloud, and
carries footprint geometry + property surface for filters), `filterStacItems`
(bbox + datetime range + `property_filter=key=value` (numeric or case-insensitive
string equality) + limit, chronologically sorted, plus geometry/bbox math), and
`temporalCollectionFromStacItems` / `temporalCollectionFromStacSearch`.

Client-side filtering covers both online and offline consumers — the same code
filters a synthetic local document used by tests as it does a real search
response. `properties` are not copied wholesale: only the actually-queried
fields are carried (cloud cover, platform, processing level), keeping the
adapter narrow.

Remote raster policy: the remote `href` (the STAC item's selected COG candidate)
is the scene's `path` (`/vsicurl/`-prefixed when `http(s)`) — the operator's
`GdalDatasetWrapper::open` handles it through GDAL's virtual file system. The
agent's `StacClient` and the `StacBrowserDialog` already produce the same
prefixed path (`selectCogHref`), so ingestion reuses their remote-COG gate.
A local existence probe in the temporal tools' old `collectionFromInput`
wrongly rejected remote hrefs; it now treats `http(s)://` and `/vsicurl/` as
remote data references and skips the `QFile::exists` check (a live open under
bounded timeouts is the actual validation).

## 5. Remote raster policy (#9)

The `DataManager`'s `GdalRasterSourceProvider` is private, so most of the
change is behind its existing seam, plus a few app-level fixes:

* **Provider resolution**: `normalizeRasterPath` now recognizes remote
  canonical forms (`http(s)://` → `/vsicurl/`-prefixed, already-prefixed
  `/vsicurl/` passed through), derives identity from the remote URL
  (not from a `QFileInfo` absolute path that would prepend a drive letter on
  Windows and silently corrupt the source), and derives `StorageKind::Remote`.
  `supports()` accepts remote sources by their raster-ish suffix, so STAC's
  `.tif` COG assets route correctly.
* **Existence gate**: the `!info.exists() || !info.isFile()` probe now runs
  only for local sources; remote sources are gated by a GDAL open attempt
  under bounded defaults (`GDAL_HTTP_TIMEOUT=30`, `_CONNECT_TIMEOUT=10`,
  `_MAX_RETRY=3`, `_VERSION=2`) and `Missing`/`Error` handling follows
  remote semantics (missing is unreachable, not a hard error).
* **Workspace confinement**: `McpServer::absolutePathOutsideWorkspace`
  (the `SICNU_MCP_WORKSPACE` guard) now treats remote refs as a distinct
  category — `http(s)://` and `/vsicurl/` are read-only data inputs that fix
  their scheme and do not escape the workspace path, `file://` maps to its
  local path, anything else (e.g. a `/vsis3/` that is not `/vsicurl/`) is
  rejected. `SICNU_MCP_ALLOW_REMOTE=0` restores strict local-only.
* **Scene acceptance**: `collectionFromInput` (in `temporal_collection_tools`)
  and `fromScenePaths` / `fromInlineScenes` no longer probe `QFile::exists`
  for remote hrefs.

## 6. Agent / Pi: discovery by default

Four tools (`temporal_collection_tools.h` already, plus new
`temporal_workspace_tools.h`: `temporal:list_collections`,
`temporal:get_collection`, `temporal:register_collection`,
`temporal:remove_collection`, and `temporal:ingest_stac`) plus the fixes to
the catalog wiring:

* `McpServer::kAllowed` allow-list now includes `temporal:` (previously the
  `temporal:*` tools were callable but hidden from `tools/list` discovery).
* `SpatialToolProvider::provideTools` now exposes `temporal:*` alongside
  `spatial:*` to the Copilot LLM (it previously dropped every `temporal:`
  tool).
* `pi/exp-rs-spatial.ts` default `EXP_RS_TOOL_CATEGORIES` changed from
  `meta,spatial,data` to `meta,spatial,data,temporal` so Pi's `wantedCategories`
  always bridges them (still overridable via the env var).
* `Agent::WorkspaceSnapshot` now carries `TemporalCollectionInfo` per workspace
  record (id + name + revision + scene count + bound-scene ratio + time range
  + platforms) into the Copilot LLM's system prompt header. That's how the
  agent learns what multi-temporal inputs already exist without the user
  pasting descriptor paths.

## 7. Provenance and the project lineage scan (#718 fix)

`findInputPathsInParams` used to scan only parameter keys containing
`input` (dropping real lineage edges for dNBR `"postfire"` and fusion
`"pan"/"ms"`); it now scans every parameter and is excluded only via
an explicit `excludePaths` set that callers use to exclude their own
destination path. `DataManager::findByPath` compared only
`absoluteFilePath` (silently dropping symlinked / hard-linked / case-variant
inputs from lineage); it now compares `canonicalFilePath` first where
available, falling back to absolute. Both producers — ToolCallDispatcher
and the CLI `rs_pipeline_runner` — now pass the output path as an
exclusion so a re-run that overwrites its own output never links to itself.

## 8. What still deliberately is not built

STAC network search stays an interactive browser dialog (`StacBrowserDialog`
→ `StacClient` with `QNetworkAccessManager` + `SICNU_MCP_ALLOW_REMOTE`); a full
agent-driven `STAC search(endpoint, bbox, datetime, limit, queryProp)` tool that
hits an external catalog is a documented follow-up with the same ingestion seam.
The same holds for the distributed STAC index, style preset library, atlas/report
generation, search history, chart engine, and the Temporal Transformer/ConvLSTM
model runtime — their per-layer seams are left open but not implemented, as the
goal's canonical scoring promised.
