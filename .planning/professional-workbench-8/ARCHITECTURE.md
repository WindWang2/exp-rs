# ARCHITECTURE — Professional Workbench 8.0

## Where this track sits

The UI layer (`src/app/**`) is a thin client over real services. This track
adds no execution machinery: every new seam either (a) deepens an existing
UI contract (SchemaFormBuilder), (b) adds a GUI-side asynchronous projection
service over sanctioned I/O seams (AssetPreviewService), or (c) widens an
existing pure projection (ContextRules facts + suggested next action,
AssetCatalogModel over DataManager snapshots).

## Cross-track seams (recorded per goal §Track discipline)

| Seam | Owner | This track's use |
|------|-------|------------------|
| `sicnu::geo::RasterReader::readWindowResampled` | geospatial-data-fabric-8 territory | consume only; `OverviewPolicy::Nearest` is the documented preview path |
| `HistogramWidget::analysisThreadPool()` (#797) | perf-arch goal, src/app/widgets | reused as the single bounded worker resource; no second pool |
| `DataManager` snapshots | data governance | read-only; model re-queries snapshots, never caches business state |
| `TaskCenter` task state | execution-plane-8 territory | read-only projection for ContextFacts in-flight fact |
| `ModelCatalog` manifests | model-runtime-8 territory | unchanged |
| `SelectionContext`/`CommandRegistry` | this track (workbench) | additive facts/projections with parity tests |

## Design decisions

### D1 — One preview service, not per-widget thumbnails (package E)

`AssetPreviewService` (src/app/preview/) is the single owner of catalog-scale
previews: bounded async raster thumbnails and vector previews keyed by
(source path, kind, mtime-ish identity, target size). Rationale: the 7.0
report named lazy thumbnails as the deferred seed; grep proved no existing
service; per-widget implementations would duplicate GDAL read + stretch +
QImage composition in every consumer. The service:

- runs jobs on `HistogramWidget::analysisThreadPool()` (the existing bounded
  pool — never a new global pool, per #797 rationale);
- reads through `sicnu::geo::RasterReader` with an explicit byte budget and
  `OverviewPolicy::Nearest` (the contract text explicitly invites preview
  surfaces);
- generation-counts requests; a newer request or a destroyed receiver
  invalidates in-flight work (same discipline as #797/#625);
- caches a bounded number of results (count+bytes capped, LRU);
- reports typed failures (open failed, unsupported, canceled) — never spins
  and never fabricates an image;
- is GUI-free at the core (QImage in, QImage out; callbacks marshaled to the
  caller's thread via queued signal) so it is testable headlessly.

Vector previews render a bounded feature sample with QGIS rendering
primitives on the worker thread into a QImage (QGIS remains the render
engine; no second renderer — same painter stack, off-GUI).

### D2 — SchemaForm 4.0 deepens the existing builder (package B)

SchemaFormBuilder stays the single form seam. 4.0 adds, schema-driven only:

- recursive **nested objects** (`type:object` + `properties`) rendered as an
  indented group with dot/JSON-pointer value nesting in values()/setValues();
- **object arrays** (`items: {type:object}`) rendered as repeatable item
  editors (add/remove, bounded count by schema maxItems when present);
- **dynamic enum sources** (`x-ui-enum-source`) resolved through an injected
  provider callback (the shell binds it to DataManager/ModelCatalog/temporal
  services) so long-lived forms refresh choices instead of going stale;
- **async value checks** (`x-ui-check`) executed on the analysis pool with
  generation cancellation; results are warnings, never silent blocks;
- **accessibility**: every editor gets accessibleName (label text) and
  accessibleDescription (tooltip), labels get buddies;
- scalar array syntax (comma/semicolon/newline) and typed item coercion stay
  as-is for backward compatibility (3.0 forms keep behaving).

No per-operator logic: if a behavior can be expressed by schema hints, it is
implemented against the hint vocabulary, not the operator id.

### D3 — Context facts widen additively (package C)

`ContextFacts` gains `hasInFlightTask` (TaskCenter has non-terminal tasks —
injected as a predicate so the pure layer stays GUI-free) and governance/
dataset selection facts it already partially carries; new pure
`suggestedNextAction(snapshot)` returns a stable action id + human text for
the empty/raster/vector/no-selection states. Palette and disabled-command
tooltips consume it. No enablement logic moves out of
CommandRegistry/ContextRules.

### D4 — Data manager panel gets a model/view catalog (package D)

`AssetCatalogModel` (QAbstractItemModel) over `DataManager` asset snapshots:

- lazy fetch: collection children load on `canFetchMore`/`fetchMore`;
- incremental text filter over id/display name/kind;
- bounded: filtered results render from an index projection (O(visible)),
  totals + truncation reported truthfully (`fmtTruncationSuffix` style);
- no widget-per-row (QTreeView);
- DataManagerPanel keeps its public test API (rowText, selectedAssetId,
  selection signals) so 7.0-era tests keep passing — parity is proven by
  keeping the existing panel tests green plus new model tests.

### D5 — Refused / out-of-scope items

- WorkflowRunCoordinator mutex-across-I/O (7.0 review finding): core-side,
  owned by execution-plane-8 — cross-track issue, not patched here.
- Pagination controls on dataset/experiment/model benches: the mlops-8
  branch owns dataset/experiment cores; the benches already surface truthful
  truncation. Small follow-up if mlops lands new store queries.
- OneOf/variants in SchemaForm: the current operator schema vocabulary has
  no oneOf producers (verified by grep over operator schema emission);
  implementing an editor without producers would be untestable speculation —
  refused this track, revisit when a producer exists.

## Interaction-robustness contract for all new async seams (package I)

Every async seam must survive: receiver destruction mid-flight, newer request
superseding an older one, layer/asset deletion mid-read, project switch
(cache keyed by identity incl. revision), and shutdown (no callbacks into
dead widgets; pool work items poll generation and bail). Tests assert each.
