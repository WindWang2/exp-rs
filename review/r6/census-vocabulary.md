# WP-B — Vocabulary census and cross-parity (operator / capability / intent / tool / recipe / docs), @ 1e28de867

## 1. The vocabularies and their authorities

| Vocabulary | Authority site | Size |
|---|---|---|
| Operator ids | explicit `add()` lists in the five `src/operators/*/\*_operators_init.cpp` files (see census-operators.md) | 189 = rs:157, gdal:5, io:17, opencv:6, otb:4 |
| Capability ids | `data/agent/capabilities/*.json` (15 files: change, classify, filter, fusion, geometric, inference, io, otb, preprocess, sar, spectral, spectral_transform, temporal, terrain, tools) | id fields mix family ids (`family:change`), operator ids, tool ids and one surface id (`spatial:geometric_registration`) |
| Intent ids | `src/science_context/capability_router.cpp:69-104` (`IntentSpec` table) and the closed table in `src/agent_loop/fake_seams.h:241-250` | 5 intents: `ndvi`, `evi`, `change`, `sar_change`, `classify` (router); fake_seams knows `ndvi`/`change`/`classify` |
| Agent tool names | `src/agent/spatial_tools/*.h|cpp` `name()` / `toolName()` returns; `src/agent/tool_catalog/surface_registry.cpp` (prefix policy) | 247 prefixed ids collected; key prefixes: `temporal:` (12), `spatial:` (17), plus io:/harness:/benchmark:/cartography:/mission:/model-ish sets |
| Recipe slots | `data/agent/recipes/*.json` (~120 files), `data/agent/scientific_recipes/*.json` | slot descriptions name operators/tools in prose |
| Docs examples | `docs/**` prose | mixed historical + current |

### Intent → capability mapping (both tables, verbatim ids)

- `capability_router.cpp` IntentSpecs: `ndvi`→`rs:spectral_index/ndvi`, `evi`→`rs:spectral_index/evi`, `change`→`rs:change_detection`, `sar_change`→`rs:sar_change`, `classify`→`rs:classify`. All resolve (the `/ndvi`,`/evi` suffixes are index parameters of the registered `rs:spectral_index` facade).
- `fake_seams.h:245-250`: `ndvi`→`rs:ndvi`, `change`→`rs:change_detection`, `classify`→`rs:classify`. All resolve. **PR #1407's `rs:change_detect`→`rs:change_detection` fix is intact in both files** (zero `rs:change_detect` in src/).

### `temporal:` tool space (real, complete list)

`src/agent/spatial_tools/temporal_collection_tools.h:21,35,48,61` + `temporal_spatial_tools.h:10-12` +
collection tools in the .cpp: `temporal:create_collection`, `temporal:describe_collection`,
`temporal:list_scenes`, `temporal:preflight_collection`, `temporal:get_collection`,
`temporal:ingest_stac`, `temporal:list_collections`, `temporal:register_collection`,
`temporal:remove_collection`, `temporal:anomaly_alert`, `temporal:phenology_query`,
`temporal:trend_inspect`. **There is no `temporal:smooth` / `temporal:gap_fill` tool.**

### `spatial:` tool space (real, complete list)

`spatial:assess_result`, `spatial:classification_diagnosis`, `spatial:compare_rasters`,
`spatial:geometric_registration`, `spatial:layer_summary`, `spatial:list_models`,
`spatial:raster_inspect`, `spatial:sample_features`, `spatial:sample_pixels`,
`spatial:search_capabilities`, `spatial:select_model`, `spatial:spectral_inspect`,
`spatial:terrain_profile`, `spatial:terrain_viewshed_inspect`, `spatial:understand`,
`spatial:validate_boa_physics`, `spatial:vector_inspect`, `spatial:workspace_summary`.

### Namespace map (which prefixes exist as what)

| Prefix | Operator family? | Tool prefix? | Notes |
|---|---|---|---|
| `rs:` | YES (157) | no | temporal operators live here as `rs:temporal_*` (24 of them), NOT under `temporal:` |
| `gdal:` | YES (5) | no | |
| `io:` | YES (17) | partially shared | `io:inspect`/`io:doctor` are operators; `io:probe`/`io:capabilities`/`io:product`/`io:product_plan` are spatial tools — routing is registry-truth (`src/agent/mcp_server.cpp:135-140`, `surface_registry.cpp`) |
| `opencv:` | YES (6) | no | |
| `otb:` | YES (4) | no | |
| `temporal:` | NO | YES (12 tools) | bench corpus `temporal:smooth`/`temporal:gap_fill` collide with this prefix while naming `rs:temporal_*` operators |
| `spatial:` | NO | YES (18 ids incl. `spatial:geometric_registration`) | zero stale `spatial:*` leftovers found in data/ (PR #1407 recipe fix verified: `spatial:temporal` = 0 occurrences repo-wide) |
| `harness:` | NO | YES | e.g. `harness:verify` used by bench; `harness:execute_plan` etc. in src |
| `model:` / `map:` | NO | NO | bench-internal fictitious ids (`model:run_inference`, `map:export_geotiff`) — opaque strings to the bench fake, no real counterpart |

## 2. Cross-parity results

- **capability JSONs → operators**: every operator-shaped id in `data/agent/capabilities/*.json`
  resolves against the 189. `geometric.json` also carries `"id": "spatial:geometric_registration"`
  (line 66) — that is the F13 catalog surface id of the tool (`src/agent/spatial_tools/geometric_spatial_tool.h:20`,
  `src/agent/tools/geometric_tool.h:21`), pinned in `data/contracts/contract_graph.snap.json:4688`. NOT drift.
- **intent ids → operators**: all resolve (above).
- **recipes** (`data/agent/recipes`, `data/agent/scientific_recipes`): no unresolved operator ids. The only
  non-resolving tokens are prose prefix references (`rs:temporal_*` in
  `data/agent/scientific_recipes/lab.temporal_analysis.json:166`, `data/labs/lab8_temporal_analysis.labspec.json:196`,
  `data/processing/algorithm_meta/capability/rs-spectral-index.json:16`) — intentional wildcard prose.
- **PR #1407 regression check**: `spatial:temporal` = 0 hits in src/, data/, docs/, tests/. The 16
  `temporal_*.json` recipes use `temporal:* tools` in slot descriptions (verified
  `data/agent/recipes/temporal_anomaly.json:20`). `rs:change_detection` intact in
  capability_router.cpp:89 and fake_seams.h:248. **Nothing regressed.**

## 3. The six ambiguous bench ids — evidence-based recommendations

All six survive only in `data/agent/bench/**` + `scripts/bench/generate_agent_bench_corpus.py`
(the generator is the source; the JSON files are its output). The bench harness treats tool ids as
opaque strings (`src/agentbench/fake_agent.cpp:334` only checks script steps against the case
allow-list), so nothing breaks today — but cases are delivered to real agents as `allowed_tools`,
teaching a wrong vocabulary.

### 3.1 `rs:change_detect` → **`rs:change_detection`** (mechanical)

- Occurrences (9 in corpus + 3 generator): cases `change-bi-temporal-basic.json:4,43`,
  `change-serial-threshold.json:4`, `change-service-unavailable.json:4`, `change-order-recorded.json:4`;
  scripts `change-{bi-temporal-basic,serial-threshold,service-unavailable}.json:38`;
  trace `recorded-change-clean.json:42`; generator lines 218 (`CH_TOOLS`), 221 (`CH_STEPS`), 231 (step_order invariant).
- Evidence: registered id `rs:change_detection` (rs_operators_init.cpp:161/353); PR #1407 already
  fixed every non-bench site to this spelling. Zero occurrences outside bench.

### 3.2 `rs:co_register` → **`rs:align`** (recommended; `rs:register_images` defensible)

- Occurrences (9 corpus + 3 generator): same 4 case files at `:3` (+ `:42` in change-bi-temporal-basic),
  3 scripts at `:24`, trace `recorded-change-clean.json:32`, generator lines 218/220/231.
- Chain semantics (generator SPEC, lines 216-222): step 1 takes `{pair: [t1,t2]}`, emits
  `work://case/aligned.tif`; step 2 change-detects on it. Case goal: "Co-register two dates and
  deliver a change map"; task_family `change`, CVA/change_score payload → optical bi-temporal.
- Candidate evidence:
  - `rs:align` — "Warp a raster exactly onto a reference raster's grid (CRS, origin, resolution and
    extent) so multi-input operators accept the pair; identical grids publish a lossless copy"
    (`src/operators/rs/rs_grid_operators.h:44-58`). This is exactly the pre-change-detection grid
    conformance step the script models (input pair → aligned.tif).
  - `rs:register_images` — "Cross-modal (optical-SAR) image registration … report CE90/residual-field
    quality" (`rs_register_images_operator.h:33-45`). Heavier F13 contract, tuned to optical-SAR
    matching; overkill for same-sensor bi-temporal alignment but the right pick if the intent is
    true image-to-image registration.
  - `rs:sar_coregister` — SAR-package-C operator (`rs_sar_coregister_operator.h:25`); wrong modality.
- **Recommendation: `rs:align`** for all 12 sites (minimal exact semantics, optical chain).
  If reviewers prefer the registration-quality narrative, `rs:register_images` is the fallback —
  but pick one; both are registered so either is MSVC-safe and capability-visible.

### 3.3 `rs:train_classifier` → **`rs:supervised_classification`** (train mode)

- Occurrences (6 corpus + 2 generator): cases `classification-{supervised-basic,ensemble-transient,rogue-recorded}.json:3`;
  scripts `classification-{supervised-basic,ensemble-transient}.json:22`; trace `recorded-rogue.json:45`;
  generator lines 173 (`CLS_TOOLS`), 175 (`CLS_STEPS`).
- Chain semantics: step 1 `{labels: work://case/labels.gpkg}` → `model.json`; step 2 `rs:classify`
  applies it. `rs:classify` is a real model-task operator (rs_operators_init.cpp:290) and stays.
- Evidence: `rs:supervised_classification` documents exactly two modes — "A) Train+predict: provide
  `training` polygons … B) Predict-only: provide `modelIn` … modelOut (string, optional) save model
  after training" (`rs_supervised_classification_operator.h:19-36`). The bench step is mode A with
  `modelOut`. No "train" operator exists in the model-task set (`rs:segment|detect|embedding|classify|change|regress`,
  `rs_model_task_operators.h`). Recipe corpus corroborates: `data/agent/recipes/classify_supervised*.json`.

### 3.4 `rs:pan_sharpen` → **`gdal:pansharpen`**

- Occurrences (2 corpus + 2 generator): `data/agent/bench/cases/optical-redundant-ndvi.json:5,47`;
  generator lines 165 (`NDVI_TOOLS + ["rs:pan_sharpen"]`), 169 (`redundant=["rs:pan_sharpen"]`).
- Semantics: it appears ONLY as a `redundant_tools` decoy the agent should NOT call. The id must
  still be a real, resolvable operator so that a wandering agent fails for the right reason
  (waste scoring), not "tool does not exist".
- Candidates: `gdal:pansharpen` (dedicated pan-sharpening operator, gdal_operators_init.cpp:23/43)
  vs `rs:fusion_brovey` / `rs:image_fusion` (fusion facade whose methods are linear/brovey/pca/ihs/gram_schmidt
  — `rs_image_fusion_operator.h:15-23`; note: no "pansharpen" method string in the rs facade).
- **Recommendation: `gdal:pansharpen`** — exact name/semantics match, registered, unambiguous decoy.

### 3.5 `temporal:smooth` → **`rs:temporal_smooth`**

- Occurrences (10 corpus + 2 generator): cases `temporal-{smoothing-basic,phenology-explain,gap-fill-transient,budget-tight}.json:3`;
  scripts `temporal-smoothing-basic.json:25`, `temporal-phenology-explain.json:25`,
  `temporal-gap-fill-transient.json:40`, `temporal-budget-tight.json:29,58,73`; generator lines 266
  (`TMP_TOOLS`), 268 (`TMP_STEPS`).
- Evidence: `rs:temporal_smooth` = "quality-aware smoothing of a per-pixel time series"
  (`rs_temporal_smooth_operator.h:2-24`). `temporal:smooth` does not exist in the temporal tool
  space (§1); the `temporal:` prefix is owned by collection tools, so the current id reads as a
  missing tool and collides with a live namespace.

### 3.6 `temporal:gap_fill` → **`rs:temporal_gap_fill`**

- Occurrences (5 corpus + 2 generator): cases `temporal-{budget-tight,gap-fill-transient,phenology-explain,smoothing-basic}.json:4`;
  script `temporal-gap-fill-transient.json:25`; generator lines 266, 311 (retry dict override
  `tool="temporal:gap_fill"`).
- Evidence: `rs:temporal_gap_fill` = "time-aware interpolation of missing (masked/NaN) values"
  (`rs_temporal_gap_fill_operator.h:2-23`). Same namespace-collision argument as 3.5.

> Any rename of these ids regenerates the whole pack and moves the pinned pack digest — see
> census-bench-corpus.md §4 for the exact regenerate-and-repin procedure.

## 4. Full stale-reference table (production code + data; excludes deliberate test fixtures)

Method: repo-wide scan of `src/ data/ docs/ tests/ examples/ tools/ scripts/ models/ pi/ platform/`
for tokens `\b(rs|gdal|opencv|otb|io|temporal|spatial):[a-z0-9_]+` that resolve against NEITHER the
189 registered operator ids NOR the live tool-name set. Raw scan kept at
`/tmp/stale_scan.txt` (189 distinct unresolved tokens; most are test fixtures). Classified table:

### 4.1 Genuine drift — src/ (should be fixed or consciously accepted)

| File:line | Literal | Should be | Evidence |
|---|---|---|---|
| `src/agent/tools/workflow_orchestrator_tool.cpp:160,184,203,220,237` | `rs:import_raster` | DECISION: no generic raster import op exists. Candidates: `io:translate` (generic convert) or sensor-specific `rs:landsat_import`/`rs:sentinel2_import`. The whole orchestrator mini-vocabulary (below) is a closed demo planner — either repoint all six ids or document the table as synthetic | emitted in every production-rule chain |
| `src/agent/tools/workflow_orchestrator_tool.cpp:206` | `rs:change_vector` | `rs:change_cva` | registered CVA operator (rs_operators_init.cpp:165); node is literally labeled "CVA" |
| `src/agent/tools/workflow_orchestrator_tool.cpp:171,209` | `rs:threshold` | `rs:threshold_raster` | registered (rs_operators_init.cpp:171/363); node params `{threshold}` match its schema |
| `src/agent/tools/workflow_orchestrator_tool.cpp:226` | `rs:gs_fusion` | `rs:fusion_gram_schmidt` | registered alias (rs_operators_init.cpp:188/380); node labeled "Gram-Schmidt" |
| `src/agent/tools/workflow_orchestrator_tool.cpp:239` | `rs:spatial_filter` | DECISION: `rs:focal_stats` / `rs:morphology` / `opencv:gaussian_blur` (node param is `kernel: gaussian`) | no registered `rs:spatial_filter` |
| `src/agent/tools/workflow_orchestrator_tool.cpp:242` | `rs:random_forest_classify` | `rs:supervised_classification` (RF model) | recipes `classify_supervised_random-forest.json`; no registered RF-specific id |
| `src/workflow/workflow_repair_engine.h:7,14,33` + `.cpp:21,72` | `rs:reproject`, `rs:convert_dtype` | `rs:reproject` → `gdal:reproject` or `io:reproject`; `rs:convert_dtype` → no registered equivalent (decision needed) | repair rule table injects these as adapter nodes; invariant is test-pinned (`tests/test_workflow_repair_rules.cpp:85,227`) so renaming means updating the pinned rule table + tests together. Note: contract-level repair may never execute against the registry — verify before renaming |
| `src/workflow/workflow_cost_estimator.cpp:58,62,64,72,74,76` | `rs:import_raster`(0.5), `rs:spatial_filter`(9.0), `rs:reproject`(6.0), `rs:threshold`(1.0), `rs:gs_fusion`(12.0), `rs:whittaker_smooth`(5.0) | re-key to the same replacements as above (`rs:whittaker_smooth` → `rs:temporal_smooth`) | closed coefficient table, "unknown ops default to 1.0" (`:56`) — benign today, drift tomorrow |
| `src/operators/otb/otb_svm_classification_operator.cpp:57` | `otb:image_classifier` | `rs:supervised_classification` predict-only (`modelIn`) or `rs:classify` | workflowHint steers agents to a non-existent OTB apply-operator; only `otb:svm_classification` (train) and 3 non-ML otb ops exist |
| `src/agent/spatial_tools/model_catalog_tool.cpp:24` | `rs:inference` (prose) | `rs:infer` | registered id is `rs:infer`; sidecars use `rs:infer` (`data/processing/algorithm_meta/rs-infer.json:3`) |
| `src/processing/framework/algorithm_meta_store.h:22` | `rs:inference` (doc comment) | `rs:infer` | same |
| `src/app/workflow/preset_catalog_widget.cpp:72,79,137,144,158,190,244,335` | `gdal:import`, `gdal:slope`, `gdal:hillshade` | DECISION — no registered counterparts (`gdal:` family is orthorectification/reproject/clip/polygonize/pansharpen). Import → `io:translate`; slope/hillshade have no registered op at all | UI workflow preset StepDef.operatorIds; verify how preset steps resolve before touching |

### 4.2 Docs drift (prose examples naming non-existent operators)

| File:line | Literal | Note |
|---|---|---|
| `docs/experiments/debugging.md:17-18,65,73` | `rs:threshold_calc`, `rs:area_stats`, `rs:stretch_linear`, `rs:histogram_equalize` | teaching walkthrough vocabulary; mirrors `tests/test_experiment_debugger.cpp` fixtures. Either lab-declared step names (acceptable) or stale — confirm with lab owner; closest real ops: `rs:threshold_raster`, `rs:zonal_stats`, `rs:contrast_stretch` |
| `docs/adr/agent-atomic-audit.md:33-34,53` | `rs:terrain_slope`, `rs:terrain_aspect`, `rs:sid_classify`, `otb:segmentation` | historical audit ADR prose; real ops are `rs:terrain_analysis` (slope/aspect outputs), `rs:supervised_classification`, `otb:meanshift_segmentation` |
| `docs/adr/0162-workflow-ir-v2-and-dag-engine.md:78,80` | `rs:reproject`, `rs:convert_dtype` | mirrors repair engine §4.1 |
| `docs/adr/0130-scientific-algorithms-foundation-4.md:16` | `rs:kmeans` | real id `rs:kmeans_classification` |
| `docs/adr/0062-unified-algorithm-execution-seam.md:10` | `gdal:gdal_translate` | illustrative; real `io:translate`/`gdal:reproject` |
| `docs/processing/grid-and-radiometric-policy.md:150` | `gdal:warp` | real `gdal:reproject`/`io:warp` |
| `docs/processing/foundation-5.md:61` | `rs:clump` | no registered clump op (closest `rs:sieve`/`rs:majority_filter`) |
| `docs/superpowers/specs/2026-07-03-processing-toolbox-phase1-design.md:157-162` | `gdal:gdal_translate`, `gdal:gdaladdo`, `gdal:gdalwarp`, `otb:otb_compute_images_statistics` | pre-implementation design doc (historical) |
| `docs/superpowers/specs/2026-07-21-unified-job-engine-design.md:97` | `gdal:warpreproject` | typo-era design doc (historical) |
| `docs/experiments/debugging.md:18` + `docs/dialog-base-class.md:125` + `docs/agent/operator-development-template.md:61` | assorted (`rs:area_stats`, `rs:my_operator`, `rs:id`) | `rs:my_operator`/`rs:id` are template placeholders — fine |

### 4.3 NOT drift (verified non-operator uses — do not "fix")

| Token(s) | Site(s) | Why it is correct |
|---|---|---|
| `gdal:geo_transform` | `src/scientific_state/asset_state_resolver.cpp:1176,1206`, `docs/scientific-state/examples/landsat_like.json:87,94` | provenance CLAIM-SOURCE label ("derived from the GDAL geotransform"), not an operator reference |
| `rs:ndvi,…,rs:nbr,rs:dnbr,rs:bsi,rs:ndre,rs:ci,rs:ndsi,rs:ndti` list | `src/operators/rs/rs_spectral_index_operator.cpp:135` (`meta["facadeOf"]`) | index-alias enumeration of the `rs:spectral_index` facade, not operator ids |
| `rs:spectral` (36 hits) | `spectral_table.h`, `rs_endmember_analysis_operator.*`, `spectral_workbench_panel.*`, `rs_spectral_reference_input.*` | comment/substring matches (`exp-rs:spectral-table` format kind, "rs:spectral workbench" prose) |
| `rs:temporal_` (18 hits) | comments + prose (`temporal_collection_tools.*`, ADR 0148/0161, labspec scenes text) | intentional wildcard prefix prose |
| `rs:obia_` (15 hits) | header title comments, ADR 0126, research notes | namespace prefix mentions |
| `io:probe/io` | `src/agent/mcp_server.cpp:137` | comment enumerating the shared io: namespace split |
| `rs:sar_` (5) | `docs/processing/sar-domain.md:475`, `test_sar_radiometric_state.cpp` header comment | prefix mention |
| `spatial:geometric_registration` | `data/agent/capabilities/geometric.json:66` + tool headers | real surface-tool id (§2) |
| `rs:inference` in `docs/adr/0122-*` | ADR prose | historical ADR; id now `rs:infer` (low-priority doc fix) |

### 4.4 Deliberate negative fixtures (do not touch)

- tests/**: `rs:noop`, `rs:x`, `rs:nope*`, `rs:__nope*__`, `rs:definitely_not_*`, `rs:no_such_*`,
  `rs:mcp_noop`, `rs:surface_noop`, `rs:surface_sleep`, `rs:stub_speckle`, `rs:dem_slope`,
  `rs:bandmath`, `rs:op`, `rs:step`, `rs:test`, `rs:one/two/three`, `rs:hallucinated_*`, `rs:obia`,
  `rs:fixture_op`, `spatial:inspect_layer` (prefix-policy sample), `spatial:__meter_probe`,
  `gdal:no_such_algorithm`, `opencv:ndvi` (wrong-op lab fixture), `rs:forbidden`,
  `rs:dangerous_export`, `rs:unmix`, `rs:teleport*`, `io:demo_*`, `io:surface_scale_probe_`,
  `gdal:contrast_stretch` (comment only), `benchmark:x` … — all synthetic ids registered in-test or
  intentionally unresolvable.
- `data/agent/evals/`: `rs:does_not_exist` (workflow_compiler), `rs:magic_ndvi_super`
  (impossible_tasks), `rs:not_a_real_operator` (anti_hallucination), `rs:teleport_band`
  (invalid_input + recovery) — the eval corpus is the negative-id oracle; these must stay.
- `data/agent/bench/traces/recorded-rogue.json:34` + generator:410 — `rs:forbidden_augment` is the
  deliberately off-list rogue tool. Must stay off the real registry, but note it is deliberately
  off-allow-list too; renaming is optional hygiene.
- `data/benchmarks/spatial_scientist_tasks.json:838,996,1025` — `rs:definitely_not_an_operator`,
  `rs:nope1`, `rs:nope2` — older benchmark fixtures, deliberate.

## 5. PR #1412 gates relevant to this WP (already exist — do not duplicate)

- CLI command-surface gate: `tests/test_cli_command_surface.cpp` (scans `dispatchCliCommandImpl`).
- Committed-help zero-diff gate: `tests/test_help_coverage.cpp:309-331` regenerates
  `docs/generated/help` (via `SICNU_REGEN_HELP_DOCS=1`) and compares committed pages.
- D8 capability-sidecar drift gate: `tests/test_capability_knowledge.cpp` +
  `src/agent/harness/capability_pages.cpp` (`gen-meta`/`gen-pages` over
  `data/processing/algorithm_meta/capability/*` and `pi/knowledge/capability-*.md`).

Note: NO gate currently checks bench-corpus ids or guidance-sidecar keyset completeness against the
registry/loader (see census-bench-corpus.md §5 and census-guidance.md §4).
