# BASELINE — Professional Workbench 8.0

Recorded at execution start, after `git fetch --all --prune`.

## Execution-time master

`origin/master` @ `2d4f0daedd` — "Merge pull request #836 from WindWang2/fix/ci-ipc-jsoncpp".
The planning-time tip (`322dfd3876`) had already moved; this audit is against
the fresh fetch. `gh pr list` and `gh issue list` returned **zero open PRs and
zero open issues** at execution start.

## Last 30 merged PRs → ownership map

| PR | Track | Relation to this track |
|----|-------|------------------------|
| #836, #835, #834, #833 | CI/SDK fixes (ipc_channel, ExternalProcess ns, range_cache/GDAL bridge) | none — infra |
| #832 | cartography-platform-7 | none (MapSpec/cartography domain) |
| #831 | verification-observability-7 | none (trace/verification) |
| #830 | plugin-isolation-runtime-5 | none (plugin runtime) |
| #829 | scientific-algorithms-7 | none (kernels) |
| #828 | execution-plane-runtime-7 | seam neighbor (TaskCenter/JobEngine) — do not fork |
| #827 | pi-spatial-scientist-harness-7 | none (agent harness) |
| **#826** | **professional-workbench-7** | **direct predecessor** — delivered §A–§F of the workbench track |
| #825 | model-runtime-multimodal-7 | seam neighbor (ModelCatalog/runtime) |
| #824 | dataset-experiment-7 | seam neighbor (DatasetStore/ExperimentStore) |
| #823 | cloud-geospatial-io-7 | seam neighbor (src/geospatial I/O) |
| earlier | 4.0/5.0/6.0 platform waves | see CONTEXT.md ADR index |

## Surviving branches / worktrees (concurrent 8.0 waves)

Worktrees present on disk at execution start:
`exp-rs-execution-plane-8`, `exp-rs-geospatial-data-fabric-8`,
`exp-rs-model-runtime-8`, `exp-rs-scientific-processing-8`,
`exp-rs-verification-platform-8`, `exp-rs-dataset-experiment-mlops-8`
(nested inside main/, planning files + docs/adr/0143 present).

`git diff --name-only origin/master...HEAD` per concurrent branch shows:
- **None of them touch `src/app/**` except geospatial-data-fabric-8 touching
  `src/app/main.cpp`** (their footprints are src/processing, src/operators,
  src/geospatial, src/experiment, src/agent, tests).
- Cross-track seam to watch: this track adds files under `src/app/**` and
  appends to `src/app/CMakeLists.txt`, `src/app/workbench/CMakeLists.txt`-style
  target source lists, and `tests/CMakeLists.txt`. Conflict risk is low but
  merges must re-run the app+tests targets.

Stale (pre-8.0) remote branches `feat/professional-workbench-7`,
`feat/professional-workbench-ux-6`, `zcode/professional-workbench-5` are all
merged historical residue (#826, #8xx merges recorded in master log) — not
divergent, not resurrected.

## Architecture ownership (verified against code, not docs alone)

| Authority | Location | State |
|-----------|----------|-------|
| Agent loop | Pi (external) + src/agent/harness | untouched by this track |
| Execution | TaskCenter (src/processing/framework/task_center.*), JobEngine, WorkflowRunCoordinator | consumed read-only by UI |
| Operators | RSOperatorRegistry | consumed via schema() |
| Models | ModelCatalog + runModelInference seam | consumed by model_workbench_panel |
| Map engine | QGIS (vendored subset src/gui, src/analysis…) | untouched except additive widgets |
| Persistence | DataManager (src/data), DatasetStore, ExperimentStore | consumed |
| Geospatial I/O | src/geospatial/raster/raster_reader.* | consumed for previews |
| Command/context | CommandRegistry, ContextRules, SelectionContext, WorkbenchHost (src/app/workbench) | **extended additively** |
| Design tokens | SicnuUi::Tokens (src/app/design_tokens.h) | consumed |

## Capability matrix (this track's packages)

Legend: Implemented / Partial / Stub / Missing / Refused-by-contract / Duplicated.

### A. UX flow audit
- **Implemented** (5.0/7.0): unified selection (SelectionContext), command
  availability + reasons (CommandRegistry/ContextRules), workspaces
  (WorkbenchHost + adapters), shutdown policy (shutdown_policy.*), processing
  history (ProcessingHistoryModel/Panel), provenance (ProvenanceSection),
  temporal workbench, dataset/experiment/model benches.
- **Partial**: some flows still rely on panel-local actions instead of
  registry commands (verified during review); documented in ARCHITECTURE.md.

### B. SchemaForm 4.0 — **the 7.0 deferral, this track's centerpiece**
- Flat scalar fields, enums, combos, units, recommended ranges, soft ranges
  (warnings), x-ui-visible-when conditionals, advanced groups, help
  enrichment: **Implemented** (SchemaFormBuilder 3.0).
- Nested objects (`type:object` + `properties`): **Missing** — buildField has
  no object branch; a nested schema property becomes a String field.
- Object arrays (`items: {type: object}`): **Missing** — arrays are one
  comma-separated QLineEdit regardless of item shape.
- Dynamic enums from authoritative services: **Partial** — the shell pushes
  static lists via set*Choices() at rebuild; no per-form provider seam, so
  long-lived forms go stale.
- Dataset/temporal/band selectors: **Missing** (asset/model/raster/vector
  exist; no dataset/temporal choice kinds).
- Async metadata checks with cancellation: **Missing** (validation is purely
  synchronous local).
- Accessibility names/descriptions: **Partial** — tooltips everywhere, but no
  setAccessibleName/setAccessibleDescription/label-buddy discipline.
- Stable round-trips: **Partial** — setValues/values() exist and are typed for
  scalars; nested/object-array round-trips undefined (fields unsupported).

### C. Context and command state
- Availability + why-disabled: **Implemented** (ContextFacts +
  unavailabilityReason + palette surfacing).
- In-flight task / dataset-run/model context facts: **Missing** in
  ContextFacts (has workbench/raster/vector/sar/edit/governance only).
- Suggested next action: **Missing**.
- "Remove scattered state logic after parity tests": audit shows scattered
  enablement remains in a few dialogs; out of blast-radius for this track
  unless verified duplicates of registry commands appear (recorded in
  ARCHITECTURE.md).

### D. Large metadata UI
- ProcessingHistoryModel: **Implemented** (bounded cap + truthful dropped
  counter + filters + O(1) cells; QTableView).
- Temporal scene model: **Implemented** (paged 200/page over filtered set).
- Dataset/Experiment/Model benches: **Partial** — first-page projections with
  surfaced truncation; no pagination controls (7.0 follow-up note).
- DataManagerPanel: **Partial → gap** — QTreeWidget with FULL tree rebuild per
  coalesced refresh (#704 fixed the storm, but cost is still O(all assets) per
  rebuild); no filter, no lazy fetch; 100k-asset projects would rebuild a
  100k-row QTreeWidget per refresh burst.

### E. Lazy preview/thumbnail/statistics services
- Histogram + ROI stats: **Implemented** (shared bounded
  `analysisThreadPool()` + generation invalidation, #797).
- Raster thumbnails: **Missing** — no thumbnail service anywhere (grep across
  src/app found none); DataManagerPanel renders text rows only.
- Vector previews: **Missing**.
- Metadata summaries: **Implemented** (DataManagerPanel detail inspector,
  synchronous local reads — acceptable, SQLite/local only).

### F. Unified professional workspaces
- **Implemented** (WorkbenchHost, IWorkbench, adapters for
  map/layout/classify/georef/OBIA/pipeline; shutdown/cancel hooks #813).
- This track audits and extends only where a verified gap appears.

### G. Task/result/provenance UX
- **Implemented** (RsJobPanel unified task center + RsResultSummary + ADR
  0132; ProcessingHistoryPanel rerun/resume; ProvenanceSection depth-6
  bounded ancestor walk).
- Gaps verified during review are logged in ARCHITECTURE.md; no fabrication
  of completion anywhere (truthful status projections tested).

### H. UI consistency and accessibility
- Design tokens + theme parity test: **Implemented** (SicnuUi::Tokens +
  test_theme_selector_parity).
- Accessible names/descriptions on schema forms and new views: **Partial**
  (covered by B/E deliverables this track).
- Literal-color audit: spot-check scheduled in review; tokens are the rule.

### I. Interaction robustness
- Shutdown/switch policy, #778 dying-layer guards, #797 pool generations:
  **Implemented**.
- New async seams (previews, async checks) inherit the same discipline and
  add dedicated stress tests: **this track's obligation**.

## Overlap map vs concurrent work (what this track must NOT do)

- Do not touch TaskCenter/JobEngine/WorkflowRunCoordinator internals
  (execution-plane-8).
- Do not touch src/geospatial I/O internals (geospatial-data-fabric-8) —
  consume RasterReader as-is.
- Do not touch ModelCatalog/runtime internals (model-runtime-8) — the model
  bench consumes manifests only.
- Do not touch dataset/experiment store cores (dataset-experiment-mlops-8 —
  its worktree shows src/agent + src/experiment + docs edits; zero
  src/app/workbench edits → the benches are safely mine).
- Do not touch operator kernels (scientific-processing-8).

## Environment (local evidence base)

- Linux 6.18 LTS x64, 16 cores, 62 GB RAM (~38 GB available), 88 GB free disk.
- cmake 4.4.3, Ninja, gcc, ccache (warm cache from main@master builds, 187K
  cacheable entries).
- Worktree build: `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_CXX_FLAGS=-fpermissive -DENABLE_TESTS=ON
  -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_{C,CXX}_COMPILER_LAUNCHER=/usr/bin/ccache`.
- Build/tests run with the resource policy from the goal (parallel 2; tests
  sequential).
