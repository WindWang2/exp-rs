# Changelog

All notable changes to the `exp-rs` project will be documented in this file.

## [Workbench 9.0] - 2026-09-12

### 🚀 Professional QGIS Remote-Sensing Workbench 9.0 (feat/professional-workbench-9)
- **Workbench state model (M1)**: `WorkbenchStateModel` — single aggregation point for project phase / tool mode / in-flight task / broken-layer facts with pure `WorkbenchRules` projections; empty-state switches now derive from one signal path.
- **Command & shortcut authority (M2)**: 22 registry-backed menu items consume `CommandRegistry` projections (single shortcut owner; 19 of them were previously double-shortcut-authority); workflow.new/open/save/run/stop join the registry; two mechanical gates (cross-source shortcut union; tooltips may not claim unbound shortcuts).
- **UI safety burn-down (M0)**: `marshal_ui.h` completion-delivery helper; histogram scan failures surface on the GUI thread; ROI request epoch made atomic; per-owner scan-pool generation regression tests; 48-cycle project clear/import/view-churn stress.
- **SchemaForm Host 5.0 (M6)**: production `WorkbenchEnumProvider` (layers/assets/models, 200-entry cap with truthful truncation) installed in TaskPanelHost; model/asset ports resolve live instead of rendering empty.
- **Large catalog UX (M7)**: bounded pagination over the filtered catalog — exact slices and totals, selection identity survives page flips; 200k logical records stay browsable in bounded windows.
- **Plugin declarative UI placement (M8)**: shell-side integration of protocol 1.1 — describe → host-render → attach through the reverse-ownership sink, production invoke delegate, and one registry command per rendered menu contribution (auto-disabled on unload/crash).
- **Master build repair**: GDAL 3.13.3 `count`-parameter type compat in canonical metadata; experiment run bridge const-qualification (GCC 16).

## [Unreleased] - 2026-09-11

### Cloud-Native Geospatial Data Fabric 8.0 (goal series)

- **Range-cache handler lifetime fix (P0)**: the `/vsirangecache/` VSI handler
  was registered from static storage while GDAL's `VSIFileManager::RemoveHandler()`
  deletes the registered handler — the first uninstall left a deleted object in
  use (use-after-free during the run, double free at process exit; reproduced
  with ASan on GDAL 3.13.3). Handlers are now heap-allocated per install cycle
  and owned by GDAL; re-install never re-registers.
- **Test-fixture teardown hardening**: `http_range_server`/`http_stac_server`
  destructors could hang forever when a client connection outlived the last
  request (Linux does not wake `accept()` on a closed listener). Destructors
  now wake the accept loop with a loopback connect, accepted sockets carry a
  bounded receive window, and the in-flight client is shut down on teardown.
- **Remote identity as a first-class contract (8.0)**: new
  `remoteIdentityToken()` derives a fail-closed, credential-safe identity
  token (`ri1:v1:<sha256>`) for remote inputs — non-empty only with a strong
  ETag; credential-shaped query values are stripped from the basis so
  re-signed URLs keep a stable identity. A self-contained FIPS 180-4 SHA-256
  backs the token (`src/geospatial/util/sha256.*`), validated against the
  known-answer vectors.
- **Execution-cache identity bridge activated**: the 7.0
  `execution_identity_resolver` seam (previously contract-only, no collector)
  is now installed by hosts (app, CLI) with the geospatial-backed resolver;
  unregistered remote inputs in `fingerprintInputsForOperatorParams` (generic
  and scene paths) fingerprint through the token instead of failing, and stay
  uncacheable (fail-closed) when no provable identity exists.
- **STAC datetime normalization**: Qt-free ISO-8601/RFC 3339 instant parsing
  (mixed offsets, assumed-UTC flagging for offset-less values, strict
  refusals); `StacItem` carries normalized UTC forms (wire forms stay
  verbatim); `buildTemporalSeries` orders by parsed instants with
  deterministic tie-breaks, and `buildTemporalSeriesDetailed` reports
  duplicate acquisition instants (kept, never dropped).
- **Range-cache 8.0 fault evidence**: concurrent-reader dedup byte
  accounting, adjacent/overlapping window reads, transient mid-range
  connection resets degrading to the `/vsicurl/` fallback without wrong
  bytes, changed-content-under-same-URL generation invalidation, and
  COG overview/window reads with byte accounting. `updateEntrySize` no longer
  fabricates a zero size when an Unchanged revalidation proves nothing about
  size.
- **Multidim string datetime axes**: `DimensionInfo` captures string
  coordinate axes (CF datetime labels) alongside numeric ones, with symmetric
  JSON round-trip (7.0 dropped numeric axis values from `toJson` — fixed);
  `MultidimView::resolveCoordinateIndexByString` selects by exact label or
  offset-normalized equal instant and refuses everything else; an end-to-end
  EO-cube workflow (instant selection → bounded window → `maxCells` refusal)
  is exercised over netCDF-4.
- **GeoParquet certified round-trip**: the foundation writer produces
  GeoParquet (driver-gated, create-capability checked); round-trip fidelity
  for field types, null vs empty, polygons, projected CRS and null geometry
  is test-certified on GDAL 3.13; the format profile upgrades to Certified
  with honest capability gating (GDAL `CreateCopy` into Parquet is
  unavailable in several builds and is no longer assumed).
- **Data Doctor 3.0 additions**: `cacheability` and `reproducibility`
  verdicts over the probed remote identity (advice-only), categorical
  `resampling_risk` for palette/QA/classification bands, and `multidim_axes`
  posture reporting numeric/string/bounded coordinate-axis coverage.
- **CLI**: `data identity <url> [--revalidate]` (bounded identity probe with
  provability verdict) and `data cache check <url> [--bytes N]` (bounded
  through-the-cache read with cache telemetry delta and config), both
  projection-only over the geospatial contracts.
- **Docs**: `docs/io/cloud-credentials.md` documents the provider-neutral,
  credential-safe boundary (CPL owns credentials; redaction vocabulary; the
  surfaces that must never carry credentials); `docs/io/certified-formats.md`
  and `docs/io/stac-interop.md` updated for the 8.0 behavior.

### Scientific Remote Sensing & Geospatial Algorithms 8.0 (goal series)
- **Forward Range-Doppler geocoding (`rs:sar_geocode`, package A)**: the 7.0
  backward orbit product is closed into a full geocoding chain — every DEM
  map-grid cell goes through forward range-Doppler (bounded bisection on the
  declared orbit contract) to a source SAR position, bilinear/nearest
  radiometry resampling through a byte-budgeted window (per-pixel 2x2
  fallback — never a whole-image read), and real-line-of-sight geometry:
  reference + facet incidence, real look-elevation layover/shadow classes,
  and the Ulander sin(theta0)/sin(thetaL) area factor as gamma0 RTC.
  Fixed five-band product with per-cause NoData counters; typed refusals for
  incomplete/contradictory orbit contracts, CRS-less DEMs and rotated grids.
  `tests/test_sar_geocoding.cpp`: analytic circular-orbit known answers,
  backward round-trip closure < 1 mm, independent-vector-math facet
  validation, operator E2E and refusal matrix.
- **Multi-date SAR statistics (`rs:sar_temporal_stats`, package B)**:
  per-pixel linear-domain mean/dispersion/cv over N >= 2 co-registered
  scenes with dB reporting and speckle-robust log-domain change against the
  per-pixel median baseline; declared-domain resolution with the dualpol
  rule (mixed declarations are typed refusals); minValid gating.
  `tests/test_sar_temporal_stats.cpp`: closed forms, threshold counting,
  invalid-sample bookkeeping, domain conversion, refusals.
- **Raster-vector analytics (`rs:rasterize`, `rs:zonal_stats`, package D)**:
  one shared windowed rasterization seam (`rs_raster_vector`) so burn
  semantics cannot drift between the operators. `rs:rasterize` burns
  constant or numeric-attribute values onto a reference grid (last-wins
  overlap, ALL_TOUCHED, NaN NoData); `rs:zonal_stats` computes exact
  per-(zone,band) count/nodata/min/max/mean/stddev (Welford) plus a
  budgeted exact median over zones streamed through the geospatial
  VectorReader contract with declared CRS transforms. Byte-budgeted feature
  cache — oversized vectors are typed refusals, never unbounded buffers.
  `tests/test_raster_vector.cpp`: analytic zones, window-spanning
  accumulation, sentinel exclusion, CRS84-to-UTM transforms, refusals.
- **Spectral formula drift guard (package E)**:
  `tests/test_spectral_formula_drift.cpp` pins every `rs:spectral_index`
  schema-enum index to an independent implementation of its documented
  formula and enforces schema-enum/table coverage plus the degenerate-
  denominator NaN contract — kernel constant changes now fail the suite
  until both sides move together.
- **Docs**: `docs/processing/sar-domain.md` sections 4-5 (geocoding,
  multi-date SAR statistics authority) and `docs/processing/raster-vector.md`
  (raster-vector family authority); algorithm_meta sidecars for the four
  new operators (meta drift tests extended by coverage).
- **Audit verdicts (no duplicate implementations)**: the temporal family
  (Mann-Kendall/Sen, harmonic, phenology, breakpoints, decomposition,
  anomaly, gap fill) was already implemented on master with one documented
  time-axis contract; terrain/hydrology, classification and the scientific
  contract layer were audited and recorded in
  `.planning/scientific-processing-8/CAPABILITY_MATRIX.md`.

### Professional Remote Sensing Workbench 8.0 (goal series)
- **SchemaForm 4.0**: schema-driven nested objects (recursive groups with
  per-group `required`, optional-group absence semantics, depth-capped with
  JSON-editor degradation); object arrays as repeatable item editors with
  min/maxItems gating and honest bounded import (≤256, visible truncation
  hint); dynamic enum choices via an injected `SchemaEnumProvider`
  (`x-ui-enum-source`) with free-text degradation instead of dead lists;
  async `x-ui-check` value checks (path_exists) on the bounded RsScanPool
  with 350 ms debounce, generation cancellation and teardown safety;
  accessible names/descriptions on every editor at every nesting level.
- **AssetPreviewService**: the single bounded async owner of catalog
  previews — raster thumbnails through `RasterReader::readWindowResampled`
  (Nearest overview policy), vector previews through QGIS's
  `QgsMapRendererCustomPainterJob` with a typed >200k-feature refusal; LRU
  cache (64 entries / 32 MiB) keyed by path+size+mtime+target; supersede,
  cancel and dead-receiver delivery drops (no UAF after teardown). The Data
  Manager detail pane consumes it lazily on selection.
- **Data Manager catalog scaling**: light incremental `AssetCatalogIndex`
  maintained from per-asset signals (refresh no longer re-fetches every full
  snapshot); coalesced filter box (name/source/id substring); lazy
  collection children above 50 (populate on first expand); bounded
  standalone rendering (default 20 000 rows) with a truthful truncation
  sentinel naming exact totals; selection preservation unchanged.
- **Context facts 8.0**: `SelectionContextSnapshot.hasInFlightTask` (shell-
  injected TaskCenter predicate) and `ContextFacts.hasBrokenLayer` /
  `hasInFlightTask`; `ContextRules::suggestedNextAction` — a deterministic,
  registry-anchored "what next" projection with unit-tested priority.
- Contracts documented in `docs/ui-architecture.md` Part IV (§21–§24);
  tests: `test_schema_form_4`, `test_asset_preview_service`,
  `test_asset_catalog_index`, `test_context_facts_8`.


### Dataset / Experiment / Scientific MLOps 8.0 (goal series, ADR 0143)
- **Automatic execution→experiment lifecycle wiring**: the 7.0
  `ExperimentRunRecorder` (previously called by nothing) is now driven by the
  authoritative `WorkflowRunCoordinator` lifecycle through a two-layer
  bridge — a workflow-free state machine (`ExperimentRunBridge`,
  `sicnu_experiment`) plus a thin workflow adapter
  (`sicnu_experiment_bridge`). Tracked workflow runs record truthfully as
  Created/Running/Succeeded/Failed/Cancelled/Interrupted; a resumed
  execution continues the SAME experiment record; terminal events for
  unknown executions are typed errors, never fabricated history.
- **Truthful interruption + stale reconciliation**: `markInterrupted` /
  `markResumed` on the recorder; at recording-enable time recorded
  non-terminal runs are reconciled against checkpoint evidence (flock-probed
  live owners are never touched; completed checkpoints without artifact
  evidence are reported, not closed as success).
- **MCP opt-in recording**: `run_workflow` accepts `experiment_db`,
  `experiment_id` (+ name/objective), optional `dataset_db`-verified pins
  (`dataset_version`, `split_manifest`, `model_id`, `model_digest`, `seed`);
  absent arguments leave behavior unchanged. Recorded runs are visible
  through the existing read-only `experiment:`/`reproducibility:` tools and
  CLI verbs.
- **Step-level provenance in auto-recorded runs**: per-step summaries
  (operator, status, error, output path/size/content digest) and the
  workflow definition snapshot ride the run's evidence document, bounded
  (≤256 steps).
- New tests: `test_mlops8_bridge` (lifecycle truth matrix, pins, stale
  decisions) and `test_mlops8_e2e` (real tracked pipelines recorded
  end-to-end, cancel/interrupt/resume stories).

### 🧭 Pi Spatial Scientist Harness 8.0 (goal series, ADR 0144)

- **Typed spatial context 2.0**: `DatasetUnderstanding` keeps every product
  fact in its own typed slot (`sensor`, `product_type`, `product_id`,
  `processing_level`, `acquisition_time`, `radiometric_state`), fixing the 7.0
  builder that folded all `SICNU_*` metadata into `radiometric_state` (last
  key won, so spacecraft/acquisition dates clobbered the radiometric state);
  adds sparse per-band `nodata`, `quality_masks` (cloud/QA band roles), and a
  bounded `SICNU_*` passthrough. `ContextLedger` records per-asset contexts
  with read-time stale detection (file identity) and bounded model
  contracts/readiness; `harness:context` surfaces `asset_contexts` and
  `model_contracts`.
- **Capability knowledge graph completion (Area A)**: knowledge entries gain
  a `surface` discriminator (`operator` | `spatial_tool` |
  `data_platform_tool`); the catalog now covers every registered operator
  (132, was 91) — preprocess, spectral-transform, filter/morphology, OpenCV,
  OTB families — plus the platform tool families (`cartography:*`,
  `workflow:*`, `style:*`, `template:*`, `solution:*`, model selection,
  `dataset:*`, `experiment:*`, `reproducibility:*`). Drift tests pin: every
  registry operator and every platform tool carries knowledge; tool-surface
  entries resolve against their authoritative registry; recipe `capabilities`
  chains resolve in registry + knowledge.
- **Evidence sidecars (Area F)** — the 7.0 known gap closes: harness-owned
  atomic writers beside each artifact produce `<out>.verification.json`
  (verdict, checks, expectations, quality summary, run identity) and
  run-identity `<out>.provenance.json` when the engine did not write one
  (never overwritten); `<out>.uncertainty.json` is written ONLY from
  operator-declared result facts (closed key list) — no fabrication; a
  declared-but-unwritable uncertainty sidecar is error-class. Run documents
  gain an `evidence[]` block; the Tier-B suite asserts sidecars land on disk.
- **Plans 8.0 (Area E)**: identity `pins` (datasets/model) validated against
  resolved entities at execute — mismatches block with the new stable code
  `IDENTITY_MISMATCH`; `cleanup` policy and step `role` vocabularies;
  deterministic `planFingerprint` recorded in bindings, run responses,
  compiled workflow `metadata`, and every evidence sidecar.
- **Intent & feasibility 2.0 (Area C)**: `harness:resolve_intent` reports
  `missing_facts` (demands vs known slots), `preparations` (static
  code→action table; unsafe fixes are marked, never guessed), and
  `solution_paths` (recipes serving the intent).
- **Explainability (Area J)**: new `harness:explain {run_id, plan?}` assembles
  data used, method applicability, execution facts, verification evidence,
  assumptions, and unknowns from authoritative stores only.
- **Preflight 8.0 (Area D)**: preflight documents expose `assumptions` (the
  warning-class issues); optical index intents on SAR-only inputs are now a
  `MODALITY_MISMATCH` blocker (fusion with an optical input still skips the
  SAR branch with a warning, unchanged).
- **Verification 8.0 (Area G)**: declared `expected_band_count` check;
  post-verification evidence checks recompute the verdict (FAIL-never-success
  preserved); meter trim path now reports `original_bytes` (7.0 pin was red
  on master).
- **Externalized eval corpus (Area I)**: `data/agent/evals/cases/*.json` —
  versioned, schema-guarded corpus (closed categories, `foreach` expansion,
  runtime fixtures, ≤ 400 cases) with a deterministic Tier-A runner
  (`test_harness_eval_corpus`); seeded with 63 expanded cases across
  anti-hallucination, invalid science, ambiguity, missing data, impossible
  tasks, multimodal, context continuation, map confirmation, and budget
  categories.
- **Test repairs of 7.0 master-red pins**: three `test_capability_drift`
  regression pins were red on origin/master (intent-only plan document;
  `original_bytes` never set by the meter trim; a text-file fixture failing
  `opens` before the advisory checks) — all repaired with the pins now
  asserting what they claim.
- `tests/helper_external_process.cpp` gains `<sys/wait.h>` (master did not
  compile the test helper on Linux/glibc).
### Execution Plane / Worker Runtime / Admission / Cache & Recovery 8.0
- **Admission scaling (cliff removed)**: TaskCenter admission now maintains
  incremental active-set counters through a single status-transition seam and
  orders launch candidates in an indexed ready heap (lazy serial
  invalidation, bounded per-pass scan, FIFO rotation for gate-held
  candidates) instead of rescanning and re-sorting the whole task map on
  every submit/transition; placeholder substitution and dispatch-fingerprint
  verification run once at staging. JobEngine's queue became priority
  buckets + an exclusive FIFO (pick O(log P) instead of full-deque scans).
  The 10k short-job drain drops from the recorded 83.3 s Debug baseline to
  seconds on this host, with launch order, never-starve and exclusive
  drain-then-alone semantics preserved.
- **Dynamic resource availability**: every resource-limit setter re-runs
  admission, so raised limits admit held work immediately (gates only ever
  delay launches).
- **Worker containment & liveness**: workers are bound to OS-level tree
  containment — POSIX session/process group with group-wide
  SIGTERM->SIGKILL ladders (a SIGTERM-immune helper is reaped; e2e tested),
  Windows kill-on-close Job Object assigned right after start. Workers emit
  optional heartbeat frames (15 s while a job runs, wire-compatible); hosts
  may enable a hang window (`SICNU_WORKER_HANG_TIMEOUT_MS`, default off).
- **Retry evidence**: the transient-failure classifier is a documented
  public seam; retry attempt/class and budget exhaustion are recorded in the
  task log and the unified trace.
- **Resume identity 3.0**: completed steps stamp their operator's
  implementation identity (schema + determinism grade + contract + platform)
  into the checkpoint (additive optional field); resume re-executes when the
  current identity differs or cannot be proven (fail-closed). A moved output
  whose checkpoint records a content digest is re-hydrated from the
  content-addressed pool only after the restored bytes re-prove the digest.
- **Remote identity in the execution fingerprint**: remote http(s) inputs
  resolve through a strong-ETag-only identity resolver (bounded session
  cache, TTL, no network under any lock — warmed before the scheduler lock),
  installed by TaskCenter unless a host wired its own; the token rides the
  canonical fingerprint as an additive optional field. Weak/inconclusive
  verdicts stay uncacheable.
- **Observability**: bounded trace events for admitted/held/retry/cancel/
  terminal/cache and resume served/rehydrated/operator_changed transitions
  (one relaxed atomic load when tracing is off).

### Intelligent Cartography, MapSpec & Template Platform 8.0
- **Raster NoData wired into the QGIS renderer (closes the 7.0 known
  limitation)**: `style:apply` pushes `raster.nodata` into the renderer —
  the declared `value` becomes a provider user-nodata range on the declared
  band, `transparent: false` shades nodata pixels through
  `QgsRasterRenderer::setNodataColor` (new optional `color`, default
  black); re-apply is idempotent; `buildRasterRenderer` carries the shading
  so the knowledge path and the live path agree.
- **Locator connector graphics**: `inset_maps[].locator.connector` compiles
  to a QGIS-native polyline (`QgsLayoutItemPolyline`, new LayoutService
  `line`/`polyline` item type) from the inset frame edge to the referenced
  frame's projected extent anchor; deterministic geometry; validated shape.
- **MapSpec v5 (strict superset)**: envelope `output` delivery declaration
  (`formats: png|pdf`, `dpi` 72..1200, optional `dir`) validated and
  surfaced through `cartography:compose` / Harness `confirmMapOutput`
  (compilation never auto-exports); per-item `binding` shape validation
  (string mode/layer/field/expression, bounded inline data, square ≤24×24
  matrices); `upgradeMapSpec` stamps v≤4 documents to 5.
- **Page-aware solver evidence**: `keep_with`/`avoid_overlap` refuse pins
  that would push a companion past its own page bottom with a
  `page_overflow` reason carried into `unsatisfied`/`violated`/decisions
  (violation reasons now preserve the permanent-failure cause) instead of
  silently writing off-page geometry.
- **Typography 2.0 additions**: declared `font.break_policy`
  (`none | halfwidth` — deterministic line-final CJK closing-punctuation
  compression) and `font.line_height` (leading override), consumed by the
  wrap-aware overflow rule; defaults reproduce 7.0 output exactly.
- **NoData legend QA**: new `MAP_NODATA_LEGEND` preflight rule (legend
  referencing a style that declares `raster.nodata` must mention NoData)
  with a converging repair stamping `legend.nodata` from the style; the
  compiler renders the entry as a QGIS-backed swatch composite.
- **Compose identity**: `cartography:compose` returns the rendering-free
  `structural_digest` plus `provenance` (declared template + component
  references) and the declared output block; Harness `confirmMapOutput`
  carries them so final-map confirmation identifies what was composed.
- **Chart labels**: bar/histogram/grouped-bar category labels elide
  deterministically (matching the table/series paths).
- **Visual evidence**: the `[visual][determinism]`/`[visual][golden]` PNG
  layers are verified end-to-end on Linux (real `QgsLayoutExporter`
  renders; golden references generate and compare in tolerance); docs
  drift fixed (`mapspec-reference` current-version header, NoData wiring
  claims, limitations).

### Model Runtime & Multimodal EO Inference Platform 8.0 (goal series, ADR 0143)
- **Real ONNX Runtime lane (WP-A)**: the 7.0 ORT provider is compiled and
  executed for the first time (ORT 1.20.1, CPU EP). Real execution fixed
  three latent defects: a dangling `Ort::TypeInfo` shape view in `warmup()`
  (read freed memory), the removed `AppendExecutionProvider_CUDA(int)`
  overload, and the removed `const T*` `CreateTensor` overload; cv::Mat
  head selection now requests exactly the named output. CMake discovers
  both official SDK layouts. 11 test cases (2168 assertions) run named
  N-D multi-input inference, multi-head rank-3/4 selection, dynamic shapes,
  int64/double/uint8 transport, warmup, health/memory accounting and
  in-forward cancellation (~25 ms latency); CUDA stays capability-gated.
- **Grid/CRS authority (WP-C)**: multimodal co-registration now includes
  semantic CRS equality — identical geotransform numbers under different
  CRS are refused. `alignment: "reference"` refuses CRS-less feeds. The
  runtime never warps; caller pre-alignment is recorded via feed
  `prepared_from` provenance.
- **Temporal sequence lane (WP-D)**: `temporal_collapse: "sequence"`
  (layout NCTHW) feeds the explicit rank-5 time axis; `temporal_dynamic`
  lets the feed define T; feed timestamps are validated strictly
  increasing; per-frame quality masks follow NoData semantics across the
  whole window. `rs:infer` gains `named_inputs[]` (paths, bands,
  timestamps, quality masks, prepared_from) and local STAC collection
  expansion; remote STAC refuses instead of silently fetching.
- **Device planner 2.0 (WP-B)**: `DevicePlacementPolicy` (LowestFitting |
  LeastLoaded) placement knob plus `deviceReport()` per-device pressure
  snapshot; admission and the bounded pressure valve unchanged.
- **Pre/post completion (WP-E)**: `postprocess.class_mapping` remaps model
  classes to product classes in labels products (injective, non-negative,
  arity-checked; palette keyed by product ids).
- **Provider resilience (WP-F)**: python worker handshake capability
  negotiation (max_rank/multi_input/dtypes replace defaults); ONE
  respawn + replay per session after a mid-exchange worker death
  (live-but-stuck workers are never restarted — a request is never replayed
  into a live worker and there are never two delivered responses); exhausted
  restart budget is a typed ProviderCrash.
- **Provenance sidecars (WP-G)**: every published raster inference product
  carries `<output>.prov.json` (model identity/digest, backend/device,
  per-input grid+CRS verdicts, execution counters, band semantics)
  published through the governed staged-rename path; sidecar failure
  removes the product.
- **Performance evidence (WP-H)**: ORT cold/warm session acquire 21.9/1.2 ms,
  named N-D forwards ≈ 23k/s, in-forward cancel latency 24.9 ms
  (`benchmarks/model-runtime-4.json`, schema `model-runtime-bench-ort/1`).

### Cartography Knowledge, Template & Recipe Platform 6.0 (goal series, ADR 0135)
- **Declarative correctness fixes**: single-item `fit_content` no longer
  discarded (#781); QGIS rule-based renderers compile declared rules as
  siblings under a symbol-less root (plus bounded nested sub-rules) instead
  of inverting the hierarchy (#782); recipe gate degradation propagates per
  branch along declared `inputs` wiring instead of a recipe-global flag
  (#784); condition pruning accepts an external runtime context without the
  redundant embedded `condition_context` (#802); condition `and`/`or`
  evaluate both operands so evaluation errors never hide behind
  short-circuit and bare literals are rejected at validation (#804); token
  references resolve through multi-hop alias chains with cycle detection
  (#815).
- **Bounded constraint-graph solver (#805)**: anchors, size clamps and all
  13 constraint kinds resolve through normalize -> dependency graph (cycle
  detection) -> propagation -> bounded relaxation (<= 24 passes) ->
  collision handling -> scoring -> convergence report. Consistent layouts
  converge to declaration-order-independent geometry; contradictions and
  non-convergence are reported with the constraint ids involved; the result
  adds `constraints_total`, `passes`, `converged`.
- **Composable components (C)**: bounded role-qualified `children[]`
  grammar (depth 1, <= 16, unique roles) with materialization onto MapSpec
  items (item children win per role); shipped legend components model
  title/classes/ramp/nodata/footer.
- **Template taxonomy (D)**: closed task/medium/purpose facet vocabularies,
  parameterized page `variants`, explainable faceted search
  (`searchTemplates`/`TemplateRegistry::search`, `match.reasons`), all 56
  shipped templates annotated; `cartography:list_templates` gained
  `medium`/`purpose`/`keyword` filters.
- **Style knowledge (E)**: `applicability` surface (value domain, band
  count, modalities) with `checkStyleApplicability`; multiband band-range
  refusal; semantically wrong renderers are reported, never silently
  applied.
- **Recipe knowledge (F)**: decisionable metadata (capabilities,
  applicability, presets, limitations, expected_artifacts, quality_gates)
  validated at load; `when_slots` conjunction gate; declared outputs of
  gate-dropped steps filtered to keep plans valid; new
  `harness.flood_mapping` exemplar (optical/SAR/fusion branches).
- **Solution explainability (G)**: `solution:search` hits carry
  `match.reasons`; the envelope explains up to 10 rejections with reasons.
- **Cross-layer drift checks (H)**: `test_knowledge_drift` mechanically
  validates recipe->operator, solution->recipe/template/style,
  template->component, style->token-set and children->component references
  across the shipped catalogs.
- **Preflight & repair 6.0 (I)**: `MAP_STYLE_REF_UNKNOWN`,
  `MAP_STYLE_DATA_MISMATCH`, `MAP_UNCERTAINTY_NOTE_MISSING` semantic rule
  families; solver diagnostics surface through
  `MAP_CONSTRAINT_UNSATISFIABLE`.
- **Visual/structural matrix (J)**: constraint-heavy double-compile
  structural-hash determinism and conditional-branch compile tests join the
  existing PNG-determinism/geometry fixture harness.

### Scientific Computing & Data Foundation 6.0 (goal series)
- **P0/P1 scientific correctness remediation (23 tracked issues)**:
  Minnaert regression slope no longer inverted (k = +m; #773) with
  physically valid synthetic scenes (#806); D8 flow accumulation keeps DEM
  NoData out of the routing graph and writes the sentinel (#783); SAR
  terrain geometry consumes the antenna LOOK AZIMUTH, derived from flight
  heading + `lookDirection: right|left` or an explicit `lookAzimuthDeg`
  (#785), with `SICNU_SAR_LOOK_AZIMUTH_DEG` output metadata and split
  heading/look vocabulary; SAR speckle queries per-band NoData sentinels
  (#803).
- **Canonical scientific contracts (Foundation 6.0, Milestone B)**: new
  `processing/contracts/scientific_contracts.h` — the numeric domain
  (DN-scale vs unit reflectance) resolves ONCE PER RASTER from declared
  `SICNU_NUMERIC_SCALE` metadata or a bounded decimated whole-raster probe,
  is logged with evidence and reported in operator results (#801 tile-boundary
  seams removed from rs:spectral_index and rs:temporal_index_series);
  explicit-regime kernel variants (`eviUnit/eviDn/...`) replace per-tile
  regime guessing in streaming loops.
- **Dataset split/leakage hardening (#775, #786, #787, #788)**: spatial
  blocks are atomic split units; ratio targets use Hare-Niemeyer
  largest-remainder with zero-ratio pinning; spatial-buffer vetoed samples
  no longer starve Validation; leakage-audit spatial hashing is injective
  across negative coordinates.
- **Experiment integrity (#774, #789, #811)**: dataset deletion commits its
  transaction (no leaked SQLite write lock); the reproduction bundle
  re-applies secret filtering at the export boundary and masks
  secret-shaped parameter keys; run upsert validates inside BEGIN IMMEDIATE
  (no TOCTOU).
- **Geospatial I/O boundedness & credential safety (#776, #790, #791, #807,
  #808, #809, #810)**: URI display() redacts token-only userinfo and
  credential-shaped queries for every scheme, with an expanded denylist;
  readBlock pads edge blocks to the uniform blockSize() contract; readWindow
  enforces a 1 GiB byte budget (typed error, never bad_alloc); group publish
  backs up and restores the main file; POSIX cross-device publish falls back
  to copy+fsync+atomic rename; remote probes run under bounded HTTP
  timeout/retry config.
- **Test integrity (#806, #814, #816, #817)**: physically valid topo test
  data + inversion refusal; numerical composition-solver assertions with
  declared tolerances; a full readBlock/iterateTiles contract suite; spatial
  block-isolation assertions; new `test_scientific_contracts` target.

### Professional Remote Sensing Workbench 5.0 (goal series, ADR 0134)
- **WorkbenchHost / IWorkbench**: every professional workspace (map, layout,
  classification, georef I2I/I2M, OBIA) registers as a workbench with a
  uniform lifecycle (activate/deactivate, dirty query, close semantics,
  selection/command context). Session windows stay dedicated top-level
  surfaces (external benches) - no MDI, no compute moves. A checkable
  workspace switcher section landed in the window menu.
- **SelectionContext**: one debounced (<=150 ms) projection of "what the user
  is operating on" - canvas current layer, layer-tree selection, Data
  Manager asset selection, governance entity selection, active workbench.
  Pure ContextRules map snapshots to capability groups (raster/vector/
  SAR/edit/result/asset) and human-readable unavailability reasons.
- **CommandRegistry 5.0**: one definition per capability (single handler,
  availability predicate, canonical shortcut with duplicate rejection,
  destructive flag, explain hook). ~50 shell commands registered; ribbon
  map-tab buttons and the layer-tree context menu now project registry
  commands (enablement + reasons shared with the menu host), the layer menu
  is uniformly Chinese, and a new layer.attributeTable command landed.
- **Command palette**: keyboard-first searchable surface over the whole
  registry (fuzzy rank, bounded rows, recent commands via QSettings,
  unavailable entries visible with reasons, no handler bypass).
- **InspectorHost**: sectioned inspector following the selection context
  (lazy population, stale cancel, placeholder for empty selection) with
  built-in General/Metadata layer sections in a new inspector dock.
- **InteractiveSession contract (batch 1)**: shared lifecycle surface
  (dirty, in-flight compute, cancel through the TaskCenter seam, close
  confirmation) + classification lab adapter and window probes
  (isSessionDirty/hasInFlightCompute/cancelInFlightCompute).
- **Data/Results surfaces emit selection**: DataManagerPanel gained
  assetSelectionChanged, WorkspaceBrowserPanel gained entitySelectionChanged
  - both now feed the shell context projection.
- **Windows fresh-build fixes found by this track** (each verified):
  plugin_loader.cpp raw ::dlsym routed through the findSymbol seam;
  WorkflowRunCoordinator missing notifyRunStateLocked/runStateChanged
  declarations (PR #764 shipped a broken header); external_tool_operator
  32-bit-long jsoncpp assignment now an explicit Json::Int64 cast;
  vestigial include of main.moc removed (AUTOMOC hard-fails on the
  dangling include with no Q_OBJECT type in main.cpp).
- **Tests**: six new contract binaries - test_workbench_host,
  test_selection_context, test_command_registry, test_command_palette,
  test_inspector_host, test_interactive_session_contract (all green
  offscreen), with the 4.0 guardrails (theme parity, shortcut conflicts,
  task-center thin-client, schema form, layer bridge, workspace wiring,
  data manager) still green.
- **Docs**: docs/ui-architecture.md Part II, ADR 0134,
  .planning/professional-workbench-5/ audit + models.

## [Unreleased] - 2026-09-07

### 🤖 Model Runtime & AI Inference Platform 4.0 (goal series, ADR 0130)
- **Manifest 4.0 identity**: `id`/`model_version`/`license`/`source`/`manifest_version` fields; the artifact's SHA-256 content digest is always computed and anchors session identity (same path, different bytes never share a session); `runtime.device` token (`cpu`|`cuda`|`cuda:N`|`auto`).
- **Unified runtime contract**: `IModelRuntime` gains warmup/cancel/health/memory-estimate; deterministic device resolution (`auto` = lowest fitting CUDA device, else CPU); structured failure classification (OOM/cancel/shape/corrupt) driving the OOM ladder.
- **Authoritative tiler**: atomic publish (same-dir stage + previous-output backup + restore on failure) for raster and vector outputs; OOM retries batches tile-by-tile (batch=1 is terminal with a diagnostic); memory stays O(batch x tile); a 100k x 100k logical raster runs as bounded windows.
- **Declarative postprocess**: `output.format` (`labels` argmax raster + palette / `mask` / `confidence`); `output.detection` decode contract (v5/v8 layouts, NMS tile dedup, georeferenced GPKG/GeoJSON/SHP); unimplemented knobs keep failing loudly.
- **Authoritative registry**: `registerManifestJson`/`unregister`/`inspect`/`validateManifestJson`/`resolve(id@version)`/`health` on `ModelCatalog`; all surfaces reference models by stable id.
- **One execution seam**: `runModelInference` serves `rs:infer` (unchanged payload keys), `rs:segment`, `rs:detect`, `rs:embedding`; bounded session pool (LRU + idle eviction + per-key release + stats).
- **Failure matrix + benchmark**: 14 dedicated failure/device/pool/registry tests (corrupt bytes, OOM ladder, mid-run cancel/crash, disk-full, removed artifact, 100k logical extent, concurrent pool bounds); env-gated benchmark (cold/warm load, tiles/s, pixels/s, RSS, cancel latency -> benchmarks/model-runtime-4.json).
- **Docs**: docs/models/model-manifest.md, docs/inference/ (runtime architecture, tiled inference, device & memory policy, backend compatibility, pre/post reference, authoring guide), ADR 0130.

### 🧭 Pi Spatial Scientist & Agent Harness 4.0 (goal series, ADR 0130)

Pi stays the generic agent foundation (ADR 0122); this series makes ExpRS the
complete geospatial harness around it: stable contracts instead of prose,
deterministic science gates instead of LLM judgment, one authoritative engine.

- **Harness module (`src/agent/harness/`)**: error taxonomy, tool taxonomy +
  manifests, entity resolution, typed context, scientific preflight, plan
  model/compiler/estimates, verification, plan runner, recipes — all exposed
  as `harness:*` tools through the unified catalog (MCP allow-list and the Pi
  bridge categories extended).
- **Stable error taxonomy**: closed code table (`DATASET_NOT_FOUND`,
  `BAND_ROLE_UNRESOLVED`, `CRS_MISMATCH`, `GRID_MISMATCH`,
  `INVALID_RADIOMETRY`, `INSUFFICIENT_MEMORY`, `MODEL_INCOMPATIBLE`,
  `EXECUTION_FAILED`, `CANCELLED`, `OUTPUT_INVALID`, `MAP_PREFLIGHT_FAILED`,
  + harness-internal codes) with category / retry class / recoverable /
  suggested actions; legacy codes normalize into it.
- **Tool manifests (Phases 1/2/14)**: every catalog entry carries a bounded
  `harness` block — taxonomy `domain.action`, risk class, side effects,
  resource hints, cancellability, preconditions, expected artifacts — derived
  from `AgentMetadata` + a namespace risk table; `outputSchema` now reaches
  `tools/list` / `get_tool_schema`.
- **Dataset grounding (Phases 4/19)**: `EntityResolver` resolves `asset-N` /
  governed UUID / path / display name against authoritative registries;
  ambiguity returns `ENTITY_AMBIGUOUS` with candidates, unknown returns
  `DATASET_NOT_FOUND`. `spatial:understand` returns a typed
  DatasetUnderstanding document with deterministic single-scene modality
  inference (sar / optical / dem).
- **Typed spatial context (Phase 3)**: `harness:context` with content-hashed
  revisions — unchanged revisions short-circuit to `{unchanged:true}`.
- **Deterministic scientific preflight (Phase 5)**: intent rule packs —
  `ndvi` (NIR/Red by role or wavelength window, radiometry, NoData), `change`
  (CRS/grid/size/resolution/radiometry), `sar_change` (modality,
  polarization, calibration domain, grid), `classify` (training slots),
  `phenology` (ordering/roles) — over inspected facts; `blocked` vetoes
  execution and cannot be overridden by the model.
- **Plan lifecycle (Phases 6/7/8)**: AgentPlan v2 (goal/intent/inputs/steps/
  outputs/verification/map_output; v1 accepted) validates with typed issues
  and compiles to WorkflowDefinition JSON — the single bridge into
  `WorkflowRunCoordinator` -> `TaskCenter`; per-step + aggregate RAM
  estimates before submission; recipe step gates (`when_slot` /
  `when_param`) compile to engine DAG dependencies.
- **Automatic output verification (Phase 9)**: `PASS / PASS_WITH_WARNINGS /
  FAIL` per artifact (existence, openability, CRS, dimensions, finite/NoData
  fractions, class domain, non-empty, provenance) and for the whole run;
  FAIL forces run status `failed` — there is no false-success path.
- **Final map confirmation (Phase 10)**: map-producing plans get layout
  presence checks and a MapSpec compose -> preflight -> bounded repair loop
  through the existing cartography seams; export only on non-FAIL.
- **Structured run results + bounded retry (Phases 11/13)**:
  `harness:run_status` reports the real engine state, per-step results, and
  verification verdicts; a transient failure may auto-resume exactly once via
  the engine's own resume (completed steps never re-run).
- **Scientific recipes (Phase 16)**: five metadata recipes under
  `data/agent/recipes/` (optical vegetation, optical change, SAR change,
  land cover, phenology) instantiate plans from bound slots; operators stay
  the only algorithms.
- **Pi subagents (Phase 17)**: role cards in `pi/roles/` (data-inspector,
  remote-sensing-planner, scientific-reviewer, cartography-reviewer,
  result-verifier) with a single-writer rule; bridge default categories now
  include `harness,layout`.
- **Harness evals (Phases 18-20)**: `tests/test_harness_evals.cpp` — nine
  deterministic cases covering six RS scenarios (NDVI and optical change
  executed end-to-end through the real engine on synthetic GeoTIFFs; SAR /
  classification / phenology / paper-figure contract grades), typed-failure
  anti-hallucination checks, FAIL-never-success, and token budgets (measured:
  context 2411 B / cap 256 KiB, error catalog 1729 B / cap 8 KiB,
  50-tool manifest page 12501 B / cap 64 KiB;
  `benchmarks/harness-token-budgets.json`).
- **Docs**: `docs/agent/` gains spatial-scientist-architecture,
  tool-contracts, scientific-preflight, result-verification,
  evaluation-suite, workflow-integration, pi-adapter; ADR 0130.

### 🔌 Plugin SDK, Isolation & Extension Ecosystem 4.0 (ADR 0130)
- **Safe unload lifecycle (#747)**: one ordered unload seam — arm the execution-barrier drain (new dispatch refused with a typed failure), bounded wait for in-flight executions (`SICNU_PLUGIN_UNLOAD_TIMEOUT_MS`, default 30 s, shared deadline at shutdown; timeout refuses with `E4005 PluginInUse` and keeps the plugin loaded), release UI contributions through a shell sink, revoke registrations (including cached model-runtime sessions), `shutdown()`, delete, `dlclose` last. Owner-scoped RAII execution leases cover operator run, agent-tool execute, model-runtime factory/infer paths, and the direct RSOperatorRegistry path (JobEngine/workflow) through lease-holding operator wrappers; after unload the barrier entry stays closed under a bumped generation so stale handles refuse instead of calling unmapped code. GUI exit unloads plugins before QApplication destruction (CLI `ShutdownGuard` parity); `unloadAll` leaves a still-busy plugin mapped for process exit instead of forcing it out.
- **UI reverse ownership (#747)**: plugin docks/menu actions/preferences pages attach and release through `ExprsPluginShellUi`; plugin-created widgets/actions are detached AND deleted while the binary is still mapped, including detaching a settings page from an open Preferences dialog.
- **Real enable/disable round-trip (#755)**: disable = unload (+ `setEnabled(false)` only when the unload succeeded); enable = load + re-attach — no restart. A fresh load reopens the plugin's execution-barrier entry under a new generation (pre-unload handles stay invalid) and reinstalls manifest-declared contributions, including the atomic-catalog adapters revoke removed; the CLI `plugin enable/disable` implements the same contract. `py:` algorithms are revoked on Python plugin unload (host bridge removes catalog/registry entries; the worker daemon pops the plugin's executors); reload re-registers cleanly with no dead entries or duplicates.
- **Entrypoint containment (#756)**: `exprs::PathPolicy` — manifest `entrypoint` must resolve to a regular file inside the canonical plugin root; absolute paths, `..` components and symlink escapes rejected at validation (`E3007`) AND re-checked immediately before mapping (validation→load TOCTOU closed).
- **Workspace effect policy (#757)**: with `SICNU_MCP_WORKSPACE` set, `ExternalProcess::run` refuses before spawn (`E5005 workspace_escape`) when the resolved working directory, an argv path (except argv[0], resolved against the child's working directory), or a manifest env value escapes the workspace; the owning plugin's directory and the operator temp work directory are accepted extra roots. Declared output publish targets are gated at the operator layer. Documented honestly as a path policy, not an OS sandbox.
- **SDK portability by design (#748)**: `sicnu_sdk` is `std::filesystem`-based (discovery, package, validator, registry index); the loader uses `LoadLibraryW/GetProcAddress/FreeLibrary` on Windows and `dlopen` elsewhere. `ExternalProcess` on Windows is a typed "not supported" refusal — documented limitation, no untested claims.
- **Conformance kit (milestone J)**: `sicnu_geo_rs_cli plugin test <dir>` — PT_MANIFEST / PT_COMPAT / PT_CONTAINMENT / PT_LOAD / PT_REGISTER / PT_REVOKE / PT_ROUNDTRIP with structured JSON output.
- **Scaffolding (milestone I)**: `scripts/exprs_new_plugin.py` generates conformance-shaped plugins for all seven contribution kinds.
- **New state**: `Quiescing` appears in plugin records while an unload drains; refusal restores `Loaded`.
- **Tests**: barrier suite, unload-refusal integration, containment (validator + loader TOCTOU + swap-after-validate), sandbox policy matrix, python register→unload→absent→reload round-trip.

### 🛡️ Data Plane, Runtime, Governance & Reproducibility Reliability 4.0 (goal series, ADR 0130)
- **Governed-state data-loss invariants (#746)**: a v3 project can no longer be silently rewritten as v1 — the governed document is cached on read (even with the governance store unavailable) and re-persisted on save; a save probes store integrity (`PRAGMA quick_check`) and falls back to the cache when the store is corrupt; the cached document and v3-seen mark are cleared on project transitions (no cross-project bleed); a state that cannot produce the governed document fails the save with `workspace.downgrade_refused` instead of dropping state silently.
- **Snapshot consistency (#751)**: `GovernanceStore::checkpointForBackup()` folds the WAL (`PRAGMA wal_checkpoint(TRUNCATE)`) before the copy; a snapshot refuses with a checkpoint diagnostic when the checkpoint fails; sidecars are copied only defensively; `busy_timeout=5000` now applies to every connection (read-only ones included).
- **Explicit restore diagnostics (#752)**: `fromProjectJson` aggregates every failed upsert into `workspace.restore_failed` with per-entity counts; read-only (newer-schema) stores surface `workspace.store_read_only`; the project document wins on the next save.
- **SQLite store hardening (#758 1–4)**: every `COMMIT` checked with rollback-on-failure; unchecked single-statement writers (`addTag`, `linkRunOutput` → `Result<void>`, `upsertPathMapping`, `clearOutgoingLineage`) verify `step()` and report typed failures; alias ownership is collision-checked symmetrically (`store.alias_collision`, existing owner kept); `project:summary` reports real `COUNT(*)` totals (`entityCounts()`); bulk read paths reuse prepare-once statements (100k mirror no longer pays 2–3 prepares per row).
- **Reference-safe artifact identity (#758 5–7)**: pool eviction keeps content-addressed objects that live records still reference; incremental storage accounting removes the per-store objects-dir tree walk; asset removal deletes only the removed asset's own lineage edges (downstream provenance survives); `ReproBundleExporter` no longer const-mutates caller options.
- **Cache correctness under external mutation (#749)**: registered (non-chained) inputs are stat-bound into cache entries and re-validated at lookup; small registered inputs carry a content digest in the execution fingerprint (contract-v2 `lazyContentDigest`, `SICNU_CACHE_INPUT_DIGEST_MAX_MB` budget, default 64 MiB) so same-size/same-mtime rewrites miss; a bounded `DataManager` file watcher (`SICNU_DATA_WATCH_LIMIT`, default 4096) advances revisions through `notifyExternalContentChange`.
- **Crash-resume artifact identity (#750)**: step checkpoints record a completion identity (size+mtime always, SHA-256 within `SICNU_RESUME_DIGEST_MAX_MB`, default 256 MiB); the resume gate re-verifies and re-executes mismatched or unverifiable (legacy) outputs — foreign bytes are never fed downstream as a resumed result.
- **Truthful governance run states (#754)**: `WorkflowRunCoordinator` exposes a run-state observer (Running/Completed/Failed/Canceled/Interrupted) bound by `ProjectContext` to `WorkspaceService::recordRun`; the mirror stops fabricating `state="Completed"`; orphan results include outputs anchored only by failed/canceled/interrupted runs.
- **ImportCenter cancellation (#753)**: cancel now stops registration at the next batch boundary with a truthful partial tally.
- **Worker runtime**: `LocalWorkerPool` — bounded warm pool of isolated operator workers (handshake health checks, per-job timeout, cancel escalation, crash → typed error + replacement, lifetime recycling, telemetry, safe shutdown), shipped as a tested seam with production wiring as tracked follow-up; `runInLocalWorker` job ids made collision-free.
- **Fault matrix**: `docs/architecture/FAULT_MATRIX_4.md` — 24 failure rows (corrupt DB, hot WAL, failed COMMIT, killed worker/workflow, stale checkpoint, external replacement, cancellation, project switch, …) with expected safe behavior + covering tests; 100k scale contract re-verified after hardening (ingest ~1.1 s, paged query 76 ms, facet 5–22 ms, 1000 lookups 39 ms, 10k bulk tag 15 ms).

### 🗺️ Cartography Components, Templates & Design System 4.0 (goal series, ADR 0130/0131)
- **Design tokens**: versioned token sets (`data/cartography/tokens/`, `TokenSetRegistry`) — typography hierarchy with CJK fallback chains, spacing/lines, semantic/status colors, colorblind-safe Okabe-Ito qualitative + ColorBrewer sequential/diverging palettes, furniture metrics, chart defaults; `print`/`screen` medium variants; `scientific-light` (default) and `scientific-dark` (screen) shipped. Total, pure resolution (`resolveTokenSet`); the compiler maps tokens onto QGIS label fonts/colors, chart painter options and colorbar palette stops. Precedence pinned by tests: item fields (incl. template slot content drafts) > component variant > component defaults > tokens > built-in.
- **Component library 4.0**: schema v2 (`version`, `variants[]`, item-shaped `defaults`, `data_bindings`, `validation` hints, `compatibility`) and 47 descriptors (from 10 seeds) across 15 categories — titles/text, categorical/continuous/grouped/uncertainty/change-transition/raster-class legends, sequential/diverging/discrete/log/asymmetric/SAR-dB/probability colorbars, five QGIS scale-bar styles + dual-unit, three north arrows, graticule/metric grids, primary/comparison/small-multiple map frames + locator inset, bar/line/area/scatter/histogram/pie/time-series/class-area/confusion-matrix/metric-card charts (new `stacked_bar`, `matrix`, `metric` painter kinds), publication furniture. `source_component` references now resolve at compile time with the documented precedence; unknown refs surface as `MAP_UNKNOWN_COMPONENT` and repair strips them.
- **Template library 4.0**: 50 task-oriented templates (from 8) with single-inheritance `extends` page families (A4/A3, portrait/landscape, 16:9 screen, dark presentation, atlas map-series sheet) — classification/LULC, change + before/after, SAR backscatter/change, vegetation/water-flood/terrain/slope/crop/forest/urban/road/heatmap/time-series/uncertainty/accuracy/burned-area/snow/ship/night-lights/soil-moisture/cloud, scientific publication, report page, operational sheet, choropleth, multi-panel, locator. Slots carry item-shaped `content` drafts and component refs; layout generator rules (margins, no furniture/frames collisions) drift-tested: every template instantiates → validates → repairs → passes → compiles.
- **MapSpec 2.0 (additive)**: `style` block; item `anchor`/`min_size_mm`/`max_size_mm`/`z_index`/`page`/`binding`; `slots[]`; typed `constraints` (align/match_width/match_height/stack/distribute); `pages[]` (≤10); `page.atlas` hook (`QgsLayoutAtlas`); `page.margin_mm`. Strict validation, idempotent v0→v1→v2 migration, extract() fidelity via stamped `semantic_role` (v1 bold-heuristic misclassification removed).
- **Composition solver**: deterministic pre-compile pass (`resolveComposition`) — anchors, size clamps, constraints solved once each in declared order; mutates geometry only; unsatisfiable outcomes reported, never silently moved. Runs inside compile and the repair loop.
- **Preflight/repair maturation**: rule catalog (machine-readable via `preflightRuleCatalog()`/`cartography:list_rules`) adds title/source-note/label text overflow (platform-independent CJK-aware estimator), declared-margin violations, legend density, byte-identical-duplicate furniture (content-safe removal only), invalid chart bindings, unbalanced multi-map frames, inset placement, unknown component refs, unsatisfiable constraints. Repairs deterministic and bounded; the repair loop converges byte-identically (test-pinned).
- **Visual regression harness**: deterministic render harness over an 8-scene benchmark set (classification, change, time series, SAR, publication, multi-panel, dense legend, CJK title) — SHA-256 render determinism, geometry contracts separate from pixels, opt-in out-of-tree golden references (25% downscale, mean-abs-diff < 12); no binary fixtures committed.
- **Agent tools**: new `cartography:list_token_sets`, `cartography:get_token_set`, `cartography:validate`, `cartography:list_rules`, `cartography:catalog_index`; `compose`/`preflight` now solve and echo the resolved composition. Both acceptance intents (land-cover page with locator + class chart; before/after change page with transition legend + uncertainty note) run end-to-end in tests without manual coordinates.
- **Docs**: `docs/cartography/` — MapSpec 2.0 reference, component/template/token guides, preflight rule catalog, visual-regression methodology, generated gallery (`gallery.md` + `data/cartography/index.json`, drift-tested), migration notes, QGIS-backed limitations; ADR 0130/0131.

### 🖥️ Desktop Workbench & Unified UX 4.0 (goal series, ADR 0130–0133)
- **Unified shell**: 工作区治理 dock is now wired to the project `WorkspaceService` (previously created inert) and refreshes on store open/reopen; window title shows `<project> — SICNU GEO RS` with a dirty marker; ribbon collapse state persists; 遥感 (ADR 0099) is the single menu entry for product preprocessing/spectral/change/fusion/terrain — 栅格/分析 keep only unique entries; dead `TaskCenterDock`/`MosaicPanel` panels and the zombie `BandCompositionRail` chrome removed with their tests.
- **Schema-driven operator UI**: `SchemaFormBuilder` gains schema-validated forms (required/range/minItems/color/JSON checks, inline error marks + summary line, Run gating), new field kinds (vector, governed-asset selector with stable ids, ModelCatalog selector, CRS via `CrsSelector`, color picker, JSON editor), accessible names from schema labels, and advanced sections that collapse but stay reachable. Operator schema defaults are the single source of truth (e.g. rs:pca dialog now defaults to the schema's 0 = all bands instead of a hardcoded 3).
- **Thin-client convergence**: dialog kernels promoted verbatim to operators — `rs:band_ratio` (ratio | IHS), `rs:extract_bands`, `rs:contrast_stretch`, `rs:image_enhancement` (stretch/filter/ratio-IHS/speckle, tile-streamed), `otb:bundle_to_perfect_sensor`, `gdal:pansharpen`; the band-ratio/extract/contrast-stretch dialogs, the enhancement panel and the fusion CLI paths now submit through the Task Center seam; batch processing resolves `rs:` ids through `RSOperatorRegistry` (adapter bypass removed); a source-scan guardrail bans inline raster kernels in dialogs. IHS outputs now mask NoData to NaN (panel #380 semantics shared with the new operator).
- **Task/result UX**: `RsJobPanel` groups pipeline steps under their parent task (expanded by default) and gains a structured 结果 tab; the shared `RsResultSummary` widget renders status/context, key metrics, warnings and double-click-to-map artifacts in `TaskPanelHost` and the job panel; 工作区治理 rows open on the map via the Data/Display seam.
- **Design tokens**: `src/app/design_tokens.h` (`SicnuUi::Tokens`) becomes the single C++ owner of semantic/status colors, spacing, icon and type sizes; RsJobPanel, the georef task list and `applyDarkPalette` converge on it; `test_theme_selector_parity` now enforces QSS↔C++ token sync.
- **Keyboard/a11y**: `test_shortcut_conflicts` pins that no two actions in the shell action host claim the same key sequence.
- **New docs**: `docs/ui-architecture.md` (information architecture, form contract, task/result contract, extension rules); tests: `test_workspace_browser_wiring`, `test_rs_band_tools_operators`, `test_schema_form_builder_v2`, `test_rs_result_summary`, `test_shortcut_conflicts`.

### 🔬 Scientific Algorithms & Processing Foundation 4.0 (goal series, ADR 0130)
- **Issue #759 fixed**: `rs:temporal_breakpoints` RMSE now divides the segment RSS by valid (finite) observations only — NaN gaps no longer understate the error; `BreakpointResult` exposes `validCount`, and an all-NaN series reports NaN instead of a fictitious `0.0`. Hand-derived regression tests (√3 case) pin it (`temporal_fit.*`).
- **Non-parametric trend**: new `rs:temporal_sen_trend` operator — Sen's median pairwise day slope with the tie-corrected Mann-Kendall test (Gilbert 1987; continuity-corrected z, two-sided erfc p-value), NaN contracts for < 3 valid observations and duplicate instants, bit-exact grade; writes slope/intercept/z/p_value/n bands plus `significantPixelFraction` (`temporal_fit.h`, `rs_temporal_sen_trend_operator.*`).
- **One NoData policy**: `processing/algorithms/nodata_utils.h` centralizes declared-sentinel resolution (`bandNoDataSentinel`, NaN when undeclared) and exact float-cast validity (`isNoDataValue` — epsilons forbidden); adopted across the seven `rs:sar_*` operators, SAR calibration's incidence raster, and `rs:kmeans_classification` (which previously epsilon-dropped values near the sentinel). Pinned by `test_nodata_utils`.
- **Typed grid refusals**: dNBR refuses CRS/geotransform-mismatched post-fire rasters (previously dims-only — wrong locations sampled silently); SAR calibration refuses grid-incompatible incidence rasters; both reuse the shared `compareGrids` service (ADR 0066/0098).
- **Determinism-grade truthfulness (ADR 0124)**: `rs:ndvi/evi/ndwi/savi/ndbi/mndwi` declare `bit-exact`, matching their `rs:spectral_index` facade for the identical streaming kernel.
- **Shared change kernels**: `ChangeDetection::cvaMagnitudeBip` (the streaming CVA loop moves under the same NaN-policy owner as the per-band kernel) and `ChangeDetection::histogramBin` (one fixed-range binning convention for Otsu/Kittler/percentile and the streaming change-mask histogram).
- **Validation framework**: `docs/processing/` policy pages — numerical tolerance grades (exact / bit-exact float / tolerance, tied to ADR 0124), the ten-fixture taxonomy per family, the valid-observation denominator rule, the sample-vs-population variance table, grid/resampling refusal policy, radiometric scale/offset policy, and the temporal per-operator semantics reference with citations (Gilbert 1987, Sen 1968).
- **Validation tests added**: SAR pure-conversion IEEE NaN/domain-edge contracts; `rs:threshold_raster` first numeric known-answer test (manual at/above + 255-NoData + valid-observation counting); streaming `rs:endmember_extraction` pinned to the full-scene PPI kernel (identical indices and PPI counts — the RNG contract is now test-enforced).
- **Build fix**: `tests/test_layout_tools.cpp` restored the anonymous-namespace opener missing from master HEAD (fresh builds failed).
- **Deliberately deferred**: full-range-Doppler SAR terrain correction (the plane-fit RTC keeps its honest name), per-scene quality weighting for all temporal statistics, and consolidation of the atmospheric-correction percentile variants — each documented in ADR 0130 with rationale.

## [Unreleased] - 2026-09-05

### 🗂️ Project Workspace, Data Governance & Reproducibility Platform 3.0 (goal series, ADR 0129)
- **Stable domain identities**: `sicnu::workspace` strong ids (Workspace/Dataset/Result/Experiment/SmartCollection/Export) with entity revisions and lifecycle vocabularies (legal result transitions draft→validated→approved/superseded/archived); paths are storage locators, never identity (`data/governance/governance_types.h`).
- **Governance store**: single SQLite WAL index (`<project>.governance.db`, schema v1, forward read-only tolerance) mirroring DataManager assets with enrichment (SHA-256 content fingerprint, sensor, modality, CRS, band roles, availability), plus datasets, results+inputs+artifacts, runs, experiments, cycle-safe recursive-CTE lineage edges, smart collections, exports, path mappings, paged faceted queries and an append-only audit log (`data/governance/governance_store.*`).
- **Project Format v3**: `<sicnuDataManager version="3">` keeps v1 blocks byte-compatible and adds one `<workspace>` JSON block; v1 files migrate in memory (full mirror, non-destructive read) and upgrade on next save; unknown sections reported then skipped; a downgrade guard re-persists the cached document when the store is unavailable (`app/data_project_serializer.*`).
- **Atomic crash-safe save**: `QgsProject::writeProjectFile` writes a temp file in the target directory, fsyncs and POSIX-replaces the target — a crash leaves the old or the new file, never a truncated one (`core/project/qgsproject.cpp`).
- **Workspace services**: `WorkspaceService` facade (mirroring, tagging, datasets, result lifecycle, runs, experiments, smart collections, impact analysis, producer lookup, audit) plus RelinkService (root moves + fingerprint-verified relink), WorkspaceValidator (machine-readable diagnostics with repair suggestions), MetadataPipeline (bounded incremental async verify/enrich with GDAL structure refresh), ImportCenter (bounded incremental scan + durable dedup), ReproBundleExporter (reference-only/metadata-only/portable), SnapshotService (collision-proof project+DB snapshots with pruning), CleanupService (protected/orphan plan, rows-only execution), WorkspaceTransactionStack (undo/redo) (`data/governance/*`).
- **Agent surfaces (Phase T)**: 11 bounded structured tools — `project:summary/search/health`, `asset:inspect/validate/relink`, `collection:query`, `lineage:upstream/downstream`, `result:inspect`, `run:compare` — every listing paged (≤100), MCP namespaces registered.
- **CLI (Phase U)**: `project validate|health|search|migrate|relink|lineage|export-manifest|audit` drive the same service layer headlessly (JSON envelope, stable exit codes).
- **Workspace UI (Phase S)**: 工作区治理 dock with paged `QAbstractTableModel` (fetchMore, 200/page), text/kind/state/sensor facets, governed details pane and bounded health check — no per-asset widgets.
- **Scale contract**: 100k assets (SICNU_WS3_STRESS=1): ingest 1.0s, paged query 84ms, facet 5–21ms, 1000 indexed point lookups 42ms, 10k bulk tag 14ms, depth-64 lineage <1ms (`benchmarks/workspace-governance-3-100k.json`).

## [Unreleased] - 2026-09-04

### 🛰️ Multimodal SpatioTemporal RS Platform 3.0 (goal series)
- **SpatioTemporal observation contracts**: typed `Modality` vocabulary + `ObservationContract` view over `TemporalCollection` (`spatiotemporal_contracts.h`, alias `SpatioTemporalCollection`); STAC ingest now maps SAR (`sar:polarizations`, `sar:instrument_mode`, `eo:gsd`) and DEM products; `inspectScene` populates modality/sensor/polarizations/radiometric state from product metadata (explicit inline claims always win); `ModalityProfile` preflight facts with new gates `temporal.modality_mismatch`, `temporal.polarization_mismatch`, `temporal.polarization_partial`, `temporal.dem_unit_undeclared`.
- **SAR operator family**: kernels in `processing/algorithms/sar/` (calibration σ0=(DN²−noise)/A², backscatter conversions, DEM plane-fit terrain flattening with layover/shadow mask, speckle incl. refined-Lee + multitemporal, GLCM texture, ratio/log-ratio) behind 8 unified operators: `rs:sar_calibrate`, `rs:sar_backscatter`, `rs:sar_terrain_flatten`, `rs:sar_terrain_correction`, `rs:sar_speckle`, `rs:sar_ratio`, `rs:sar_texture`, `rs:sar_change`. The speckle dialog is now a thin client over `rs:sar_speckle` (GUI executes no kernels).
- **Temporal Analysis 2.0**: pure fit kernels (`temporal_fit.h`: Savitzky–Golay, Whittaker banded solve, harmonic regression w/ IRLS, phenology metrics, greedy piecewise-linear breakpoints, seasonal decomposition) behind `rs:temporal_smooth`, `rs:temporal_gap_fill`, `rs:temporal_harmonic_fit`, `rs:temporal_phenology`, `rs:temporal_breakpoints`, `rs:temporal_decompose`.
- **Multimodal Feature Cube**: self-describing feature stacks (`processing/features/feature_cube.*`, dataset metadata + sidecar) behind `rs:feature_stack`, `rs:feature_normalize`, `rs:feature_select`; model-input matching for `rs:infer` preflight.
- **Model Runtime 3.0**: manifest v3 `inputs[]` (named multi-input), per-input `temporal_length`/`temporal_collapse`, `output.uncertainty` (entropy/margin); `IModelRuntime::inferMulti` with named-input OpenCV DNN support; optional ONNX Runtime provider behind `SICNU_WITH_ONNX_RUNTIME` (graceful stub without the dependency).
- **Tile Inference Engine 2.0**: multi-head output stacking with `SICNU_OUTPUT_HEADS` layout metadata, softmax-entropy/margin uncertainty bands, flip-TTA averaging, VRAM/RAM-budget-aware batch sizing.
- **Agent surfaces**: `WorkspaceSnapshot` collection info + `temporal:*` tools expose modalities/sensors/polarizations.
- **Model library**: 24 new high-quality manifest templates (buildings/roads/water/landcover/crops/forest/change/cloud/ship/airplane, UNet/SAM/YOLO/SegFormer/Swin/Siamese/temporal families, optical-SAR fusion, SAR water/flood/ship) with a catalog validation test (`test_model_library_manifests`).
- **Baseline repair**: regenerated `algorithm_meta` sidecars for the #738 taskFamily drift (7 sidecars, drift test updated from the pinned 6).
### 🛰️ Pi Spatial Scientist & Cartography Workbench 3.0 (ADR 0127/0128)
- **Spatial Reasoning Contracts (`src/agent/contracts/`)**: versioned structured documents — DatasetUnderstanding, CapabilityCandidate, PreflightResult, ExecutionPlan, ResultAssessment, MapQualityReport — with builders, validators, and bounded-output helpers (`paginate`, serialized-size caps).
- **Workspace Understanding 3.0 (`workspace_state.*`)**: `WorkspaceState` document with stable entity ids (`asset-N`, `layer-N`, `collection-N`, `layout-N`) persisted in project properties; visible/selected/active layers, selected-feature counts, layouts, models, running tasks, recent outputs, and workflow-run summaries via a provider seam (no agent→workflow link dependency). Exposed through `spatial:workspace_summary` and `spatial:layer_summary`.
- **Bounded Inspection Tools**: `spatial:sample_pixels` (≤64 point reads, CRS transform), `spatial:sample_features` (≤20 attribute rows, attribute filter), `spatial:compare_rasters` (grid compatibility + decimated difference verdict: identical/within_tolerance/different/incomparable).
- **Capability Discovery (`spatial:search_capabilities`)**: fused ranking over the unified catalog — task-text relevance, band-role affinity, GPU fit, large-raster safety, determinism — returning CapabilityCandidate summaries with reasons/warnings/estimated cost; schemas stay behind `get_tool_schema`.
- **Automatic Model Selection (`spatial:select_model`)**: task-contract driven ranking over `ModelCatalog::rankModels` with readiness and artifact resolution surfaced; no hardcoded model names.
- **Static Workflow Preflight (`workflow:preflight`)**: schema/topology/operator/param/input/output/model checks with `WF_*` codes, repairability flags, and suggested actions — planners fix DAGs before executing.
- **Result Assessment (`spatial:assess_result`)**: empty-output, nodata-ratio, constant-output, value-range/class-count expectations, CRS presence, vector geometry validity → ResultAssessment verdict (pass/warn/fail) with provenance echo.
- **MapSpec (`src/agent/mapspec/`, ADR 0127)**: declarative cartographic document (page, map_frames, layers, legends, north arrows, scale bars, titles, labels, charts, colorbars, grids, annotations, source notes, constraints) with strict validation, v0→v1 migration, id-stable patch ops, `MapSpecCompiler` (MapSpec → QgsPrintLayout via LayoutService item factories) and best-effort `extract` roundtrip.
- **Component & Template Libraries (`data/cartography/`, `src/agent/cartography/registry.*`)**: 10 component descriptors (variants, layout constraints, compatibility) and 8 map templates (semantic slots → concrete MapSpec drafts via `cartography:instantiate_template`), with embedded fallbacks for headless runs.
- **Charts as First-Class Components**: `ChartRegistry` workspace entities (`chart-N`), QGIS-native `QgsLayoutItemChart` binding for layer-expression series (bar/line), QPainter inline renderer (bar/line/pie/area/scatter/histogram — no QtCharts), and color-bar rendering.
- **Compose → Preflight → Repair Loop (`cartography:compose/preflight/repair`)**: MapQualityReport with `code/severity/item_id/repairable/suggested_action` issues and 0–100 quality score; deterministic repair passes add missing furniture, move off-page items, bump tiny fonts, separate overlaps.
- **Symbology Intelligence (`symbology:describe/apply_categorical/apply_graduated/apply_raster_ramp`)**: bounded scans, bounded category counts, previous-renderer capture with rollback.
- **Workspace Command Model (`src/agent/commands/`, `workspace:undo/redo/history`)**: unified transaction stack for agent/symbology workspace mutations; layout mutations keep QGIS-native `QgsLayoutUndoStack`.
- **MCP & Pi Bridge**: `cartography:`/`symbology:`/`workflow:`/`workspace:` namespaces routed to the SpatialToolRegistry; Pi extension default `EXP_RS_TOOL_CATEGORIES` extended accordingly.
- **Benchmark**: `tests/test_spatial_scientist_benchmark.cpp` — 111 structured tasks across data understanding, tool discovery, interaction, scientific analysis, model selection, workflow, cartography, repair, and bounded-context graders, graded against live registries.
- **Tests**: new suites for contracts, workspace state/entity ids, MapSpec model/compiler/roundtrip, registries, charts, preflight/repair, symbology rollback, workflow preflight, capability ranking, and the benchmark harness.

## [Unreleased] - 2026-08-24

### 🚀 Pi-Based Spatial Intelligence Layer (ADR 0122)
- **Pi Adapter (`pi/`)**: TypeScript Pi extension (`exp-rs-spatial.ts`) bridging the exp-rs MCP server into the Pi agent runtime — spawn + handshake + `tools/list` → `pi.registerTool`, abort-aware dispatch, output truncation, and a `wait_for_execution` convenience tool. Includes the agent knowledge base (`pi/knowledge/`).
- **Spatial Tool Framework**: `SpatialTool` contract (`name/description/input_schema/execute/output_schema`) with a process-wide registry; `SpatialToolProvider` feeds the unified `AgentToolCatalog`; `spatial:` joins the MCP allow-list and executes inline.
- **Spatial Inspection Tools**: `spatial:raster_inspect` (GDAL metadata, band roles, wavelengths, nodata, radiometric state, optional subsampled statistics) and `spatial:vector_inspect` (OGR layers, schemas, extents, sampled features).
- **Agent Workflows**: MCP meta tools `run_workflow` (agent-generated pipeline JSON → `TaskCenter::submitPipelineJson`, per-step execution ids) and `get_workflow_status` (aggregate DAG status).
- **Model Runtime Catalog**: `ModelCatalog` scanning `models/*/model.json`; exposed via `spatial:list_models`; `rs:infer` resolves catalog names to weight paths.
- **Algorithm Capability Sidecars**: `AlgorithmMetaStore` over `data/processing/algorithm_meta/*.json` (task/input/output/gpu/accuracy) merged into `list_algorithms` / `search_algorithms` / `get_algorithm_schema` responses.
- **MCP `tools/list`**: now enumerates the unified tool catalog (algorithms, interaction, data, spatial) with full JSON Schemas alongside the meta tools.
- **Documentation Sync**: README (Spatial Intelligence section, corrected test count to 1,758 Catch2 cases, current architecture tree), CLAUDE.md (architecture map + language note), CONTEXT.md (new domain terms: Spatial Tool / Spatial Tool Registry / Model Catalog / Algorithm Capability Sidecar / Pi Bridge; ADR 0062–0122 index), docs/repo-layout.md, HANDOFF.md.

## [Unreleased] - 2026-08-03

### 🚀 Features & Deepening Architecture
- **Pipeline Status Enrichment**: Integrated `PipelineStatusResolver` callback injection in `WorkflowSession`, enabling authoritative DAG pipeline step completion status sync from `TaskCenter` without circular library dependencies.
- **Dynamic Worker Pool Control**: Implemented `PythonWorkerProcessPool::setPoolSize(int)` with dynamic grow/shrink capabilities and busy-worker protection.
- **Viewport Encapsulation**: Refactored `ActiveViewHost::viewportSnapshot()` to consolidate map canvas state into value-semantic `ViewportSnapshot` structs with single-point null safety.

### 🛡️ Security & Quality Fixes
- **IPC Socket Permissions (SEC-001)**: Restricted `QLocalServer` Unix domain socket permissions to `QLocalServer::UserAccessOption` (User-only `0700` access) to prevent multi-user local privilege escalation.
- **Transactional Upfront Shrink**: Added idle-count pre-validation to `PythonWorkerProcessPool::setPoolSize()` to guarantee atomicity and prevent pool size state desynchronization.
- **Qt6 Deprecation & Macro Safety**: Replaced deprecated `qMax` with `(std::max)` for header safety on Windows (`NOMINMAX`).
- **Container Growth Cap**: Enforced definition step bounds on `WorkflowSession::markStepComplete` and `snapshot()` step IDs.

### 🛠️ Agent & Tooling Integration
- **Agent Guidelines (`AGENTS.md`)**: Configured project-scoped behavioral rules integrating Andrej Karpathy's 4 core guidelines (Think Before Coding, Simplicity First, Surgical Changes, Goal-Driven Verification).
- **Skill Suites**: Installed `karpathy-guidelines` and the full `gstack` 59-skill suite for automated PR reviews, security auditing, and performance benchmarking.

---
*Historical per-sprint assertion counts above are dated evidence from their
own entries; see PROJECT.md for the current verification policy.*
