# Research: Classification mainwindow seams

**Ticket:** GitHub issue #2 — Inventory classify mainwindow seams  
**Primary files:**

| File | Role |
|------|------|
| [`src/app/classification/qgsclassificationmainwindow.h`](../../../src/app/classification/qgsclassificationmainwindow.h) | Member surface; public slots + private setup/helpers |
| [`src/app/classification/qgsclassificationmainwindow.cpp`](../../../src/app/classification/qgsclassificationmainwindow.cpp) | ~3631 lines; all clusters live here as method groups |
| [`src/app/classification/rs_classify_workflow_controller.{h,cpp}`](../../../src/app/classification/rs_classify_workflow_controller.h) | Soft-gate flags / step machine (already extracted) |
| [`src/app/classification/rs_classify_workflow_bridge.{h,cpp}`](../../../src/app/classification/rs_classify_workflow_bridge.h) | Dual-write mirror to `lab.classify.supervised` runtime |
| [`src/app/classification/rs_classify_session_state.{h,cpp}`](../../../src/app/classification/rs_classify_session_state.h) | Dirty flag + QSettings workflow snapshot |
| Supporting widgets/tasks | See per-cluster tables below |

**Scope:** inventory of existing **seams** (method/member clusters that already behave like shallow modules). Not a redesign.

**Vocabulary (light):**

- **Module** — a method/member cluster with a coherent responsibility.
- **Interface** — the thin public surface other clusters already call (often 1–3 methods).
- **Seam** — a place where those modules already separate, even if still co-located in one class.
- **Leakage** — shared mutable state written/read across seams without a single owner.

---

## Executive summary

`QgsClassificationMainWindow` is a god-object shell. Six shallow **modules** are already visible as method clusters + related members:

1. **Sample vector edit** — memory polygon layer + digitize tools + dual sync with `RsRoiCollection`
2. **Session layer tree** — local `QgsLayerTree` / store / canvas bridge
3. **Workflow chrome** — stepper + step host + controller flags + gate UI refresh
4. **Job apply / preview / post / CV** — train-matrix build + `RsJobRunner` task launches + finish lambdas
5. **Export / project** — ROI I/O, step-7 checklist, `.rscproj`, load-to-main
6. **JM / spectral** — ROI-driven separability + spectral curve docks

A seventh **cross-cutting core** (not listed in the ticket but unavoidable) is **source raster / class definitions / ROI collection** — nearly every cluster reads it.

**Worst leakage:** (a) dual source of truth `m_sampleLayer` ↔ `m_rois` with suppress flags; (b) `m_rois::changed` fan-out into dirty, workflow, spectral, JM, status; (c) job finish lambdas writing path/result/workflow/layer/accuracy state inline; (d) `m_previewLayer` reused for preview *and* full classify *and* post-process results.

---

## 1. Cluster map (methods + members)

### 1.1 Sample vector edit

**Intent:** QGIS-like training sample digitizing on a memory polygon layer; geometries are the edit surface, `RsRoiCollection` is the training cache (pixel indices).

| Kind | Symbols | Location |
|------|---------|----------|
| Setup | `setupSampleVectorEditing` | cpp ~689–760 |
| Layer lifecycle | `ensureSampleLayer`, `applySampleLayerRenderer`, `ensureSampleLayerEditing` | cpp ~762–848 |
| Edit ops | `onToggleEditing`, `deleteSelectedSamples`, `onSampleDigitized`, `onMagicWandRoi`, `onCurrentClassChanged`, `resolveActiveClassId` | cpp ~850–1078 |
| Dual sync | `rebuildRoisFromSampleLayer`, `syncSampleLayerFromRois`, `onSampleLayerEdited` | cpp ~882–974 |
| Status | `updateRoiStatusLabels` | cpp ~1080–1105 |
| Role UI (shared with workflow) | `setActiveSampleRole` | cpp ~1679–1698 |
| Nested helper | anonymous `SampleSelectTool` | cpp ~128–190 |

**Members (h ~153–164, 181):**  
`m_sampleLayer`, `m_cadDock`, `m_toolPan` / `m_toolSelect` / `m_toolAddPolygon` / `m_toolMagicWand`, edit actions, `mSuppressSampleSync`, `m_trainSampleRole`.

**Related files (already separate widgets/tools):**  
`rs_roi_tool_*.{h,cpp}`, `rs_class_table_widget`, `rs_class_quick_list`, `rs_roi_collection` / `rs_roi` / `rs_pixel_rasterizer`.

**Natural interface already used by others:**

- *Layer → ROIs:* `rebuildRoisFromSampleLayer` / `onSampleLayerEdited`
- *ROIs → Layer:* `syncSampleLayerFromRois` (load ROI / load project)
- *Class selection:* `resolveActiveClassId`, `onCurrentClassChanged`

---

### 1.2 Session layer tree

**Intent:** Session-local layer stack independent of the main project tree; source of truth for the classification canvas.

| Kind | Symbols | Location |
|------|---------|----------|
| Setup | `setupLayerManager` | cpp ~571–595 |
| API | `addSessionLayer`, `removeSessionLayer` | cpp ~597–632 |
| Callers | `ensureSampleLayer`, `openSourceRaster`, apply/preview/post finish lambdas | scattered |

**Members (h ~147–151, 222–224):**  
`m_layerTree`, `m_layerTreeModel`, `m_layerTreeView`, `m_layerTreeBridge`, `m_layerTreeDock`, `m_layerStore`, `m_sourceLayer`, `m_previewLayer`.

**Seam quality:** cleanest of the six — two methods + store ownership. Leakage is *who* holds semantic pointers (`m_sourceLayer`, `m_previewLayer`) rather than the tree API itself.

---

### 1.3 Workflow chrome

**Intent:** Wizard/expert stepper UI, soft gates, dual-write to workflow runtime.

| Kind | Symbols | Location |
|------|---------|----------|
| Setup | `setupWorkflowUi`, `populateStepPanels` | cpp ~1178–1638 |
| Sync | `syncWorkflowFromRois` | cpp ~1708–1728 |
| Paint gates | `refreshWorkflowUi` | cpp ~1730–1906 |
| Busy | `setClassifyBusy` | cpp ~1700–1706 |
| Defaults | `ensureDefaultClasses` | cpp ~1640–1677 |
| Snapshot glue | `captureWorkflowSnapshot`, `applyWorkflowSnapshot` | cpp ~316–350 |

**Members (h ~166–185):**  
`m_workflow`, `m_workflowBridge`, `m_stepper`, `m_stepHost`, `m_workflowDock`, step labels/buttons, `m_accuracyPanel`, `m_classifyBusy`.

**Already-extracted modules:**

- `RsClassifyWorkflowController` — flag authority (class count, train pixels, full result, accuracy, post, export…)
- `RsClassifyWorkflowBridge` — mirrors step/complete + artifacts (comments note dual-write is temporary)
- `RsClassifyStepperBar`, `RsClassifyStepHost` — pure chrome widgets
- `RsAccuracyPanel` / `RsAccuracyDialog` — metrics display

**Natural interface:** other clusters call `m_workflow->setHas…` + `refreshWorkflowUi()` after side effects. `syncWorkflowFromRois()` is the ROI → gate path.

**Note:** `populateStepPanels` (~380 lines) is a **UI assembly** seam that *wires* into every other cluster (open raster, export ROI, recompute JM/spectral, apply/preview/CV, post-process, export). It is chrome construction, not domain logic, but it is the densest cross-link site.

---

### 1.4 Job apply / preview / post / CV

**Intent:** Build training matrices, configure backends, launch exclusive/non-exclusive jobs, apply results to session state.

| Kind | Symbols | Location |
|------|---------|----------|
| Training matrix | `buildTrainingData`, `currentIgnoreOptions` | cpp ~1994–2153 |
| Anonymous helper | `fitScalerOntoConfig` | cpp ~192–207 |
| Apply (full map) | `applyClassification` | cpp ~2155–2420 |
| Preview (viewport) | `applyPreview` | cpp ~2428–2604 |
| Post-process | `openPostProcessDialog`, `runPostProcess` | cpp ~2606–2801 |
| Cross-validation | `runCrossValidation` | cpp ~2803–2928 |
| Classifier bar wiring | `setupClassifierBar` | cpp ~1107–1176 |
| Model load (feeds apply) | `loadClassifierModel` | cpp ~3250–3318 |

**Members (h ~187–189, 219–229):**  
`m_classifierBar`, `m_applyAction`, `m_lastClassifyPath`, `m_lastPostRasterPath`, `m_lastPostVectorPath`, `m_loadedBackend`, `m_loadedScaler`, `mLastModelPath`, `m_classifyBusy`, `m_previewLayer` (result sink).

**Related files:**  
`rs_classification_task`, `rs_post_process_task` / `rs_post_process_dialog`, `rs_cv_task` / `rs_cross_validation`, `rs_classifier_*`, `shell/rs_job_runner.h`.

**Natural interface:**

- Inputs: `m_rois` + source geo (`m_sourceRasterPath`, `m_sourceGt`, W/H) + `m_classifierBar`
- Outputs (finish lambdas): paths, session layers, workflow flags, accuracy panel, `refreshWorkflowUi`

Apply/preview/CV share a repeated pattern: busy guard → band default → `buildTrainingData` → backend switch → `RsJobRunner::run` → finish lambda. That repetition *is* the seam boundary (same inputs, different job id / crop / accuracy).

---

### 1.5 Export / project

**Intent:** Persist ROIs, classified products, accuracy CSV, project file; optional push to main window.

| Kind | Symbols | Location |
|------|---------|----------|
| ROI save | `saveRoisToPath`, `exportRois` | cpp ~352–400, ~3177–3186 |
| ROI load | `loadRois` | cpp ~3188–3244 |
| Step 7 | `exportSelectedStep7`, `copyPathWithDialog`, `loadClassificationResultToMain` | cpp ~3324–3465 |
| Project | `saveClassificationProject`, `loadProjectFromFile` | cpp ~3467–3631 |
| Session dirty / close | ctor dirty hooks, `closeEvent` | cpp ~270–275, ~402–429 |
| Snapshot | `captureWorkflowSnapshot`, `applyWorkflowSnapshot` | cpp ~316–350 |

**Members (h ~187–201, 231–233):**  
`mSession` (`RsClassifySessionState`), `mSuppressDirty`, export checkboxes/buttons, `m_accuracySource`, `m_projectPath`, path members shared with jobs.

**Related files:**  
`rs_roi_io`, `rs_classification_project` (outside this dir but used here), `rs_classify_session_state`.

**Natural interface:** reads last-job paths + ROI collection + workflow flags; writes dirty/paths/workflow restore.

---

### 1.6 JM / spectral

**Intent:** Sample quality diagnostics from ROIs + source raster; docks + toolbar toggles.

| Kind | Symbols | Location |
|------|---------|----------|
| Spectral | `recomputeSpectralCurves` | cpp ~2930–3064 |
| JM | `recomputeJmMatrix` | cpp ~3066–3175 |
| Throttle | `m_jmRecomputeTimer` wired in ctor | cpp ~252–269 |
| Dock setup | `setupDocks` (jm/spectral docks) | cpp ~667–676 |
| Toolbar visibility | Spectra/Separability actions in `setupClassifierBar` | cpp ~1141–1170 |
| Wizard visibility | `refreshWorkflowUi` expert/eval dock show/hide | cpp ~1872–1892 |

**Members (h ~205–211, 226):**  
`m_jmDock`, `m_spectralDock`, `m_spectralCurve`, `m_jmMatrix`, `m_jmRecomputeTimer`.

**Related files:**  
`rs_spectral_curve_widget`, `rs_jm_matrix_widget`, `rs_jm_separability`.

**Natural interface:** pure consumers of `m_rois` + source geo + band selection; write only into their widgets. Triggered by `m_rois::changed` (auto) or step-3 buttons (manual).

---

### 1.7 Cross-cutting core (not a ticket cluster, but shared hub)

| Kind | Symbols | Location |
|------|---------|----------|
| Source open | `openSourceRaster` / `openSourceRaster(path)` | cpp ~1908–1992 |
| Class defaults | ctor seed + `ensureDefaultClasses` | cpp ~224–237, ~1640–1677 |
| Shell chrome | `setupMenus`, `setupToolbars`, `setupDocks`, `setupStatusBar` | cpp ~432–687 |

**Members:** `m_iface`, `m_canvas`, `m_rois`, `m_sourceRasterPath`, `m_sourceWidth/Height/BandCount`, `m_sourceGt[6]`, class table/quicklist docks.

This hub is the **main leakage surface**: almost every cluster depends on it without going through a single owner API.

---

## 2. Dependency sketch

Arrows mean “calls / writes into / reads from”. Solid = method call; dashed = signal or shared member.

```
                    ┌──────────────────────────────┐
                    │  Core hub (shared state)     │
                    │  m_rois, m_source*, m_canvas │
                    │  m_classTable / QuickList    │
                    └──────────────┬───────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
          v                        v                        v
 ┌─────────────────┐    ┌──────────────────┐    ┌───────────────────┐
 │ Sample vector   │◄──►│ Session layer    │    │ JM / spectral     │
 │ edit            │    │ tree             │    │ (read-only of hub)│
 │ m_sampleLayer   │    │ add/remove       │    └─────────▲─────────┘
 └────────┬────────┘    └────────▲─────────┘              │
          │ dual sync            │ add result layers      │ m_rois::changed
          │ rebuild/sync         │                        │ + timer
          v                      │                        │
     (hub m_rois) ───────────────┼────────────────────────┘
          │                      │
          │ syncWorkflowFromRois │
          v                      │
 ┌─────────────────┐             │
 │ Workflow chrome │◄────────────┤ setHas* / setClassifyBusy
 │ controller+UI   │             │ refreshWorkflowUi
 └────────▲────────┘             │
          │                      │
          │ gates enable buttons │
          │                      │
 ┌────────┴────────┐    ┌────────┴─────────┐
 │ Job apply/prev/ │───►│ Export / project │
 │ post / CV       │    │ paths + .rscproj │
 │ m_last* paths   │───►│ + load to main   │
 └─────────────────┘    └──────────────────┘
```

### 2.1 Who calls whom (cluster → cluster)

| From → To | How |
|-----------|-----|
| Sample edit → Core hub | `rebuildRoisFromSampleLayer` writes `m_rois`; digitizing uses `m_sourceGt` for px_count |
| Sample edit → Session tree | `ensureSampleLayer` → `addSessionLayer` |
| Sample edit → Workflow | via `m_rois::changed` → `syncWorkflowFromRois` (not direct) |
| Core hub → Sample edit | `openSourceRaster` re-CRS sample layer, `rebuildRoisFromSampleLayer`; load ROI/project → `syncSampleLayerFromRois` |
| Core hub → Session tree | `openSourceRaster` add/remove source + re-stack samples |
| Core hub → Workflow | `setHasSourceRaster`, bridge artifact |
| Core hub → JM/spectral | `m_rois::changed` auto recompute |
| Workflow → Sample edit | role buttons → `setActiveSampleRole`; step panels only *wire* export/load ROI |
| Workflow → Jobs | step buttons → apply/preview/CV/post slots; soft-enable in `refreshWorkflowUi` |
| Workflow → JM/spectral | dock show/hide by mode/step; step-3 recompute buttons |
| Jobs → Session tree | finish lambdas `addSessionLayer` / `removeSessionLayer` |
| Jobs → Workflow | `setHasFullClassifyResult`, `setHasAccuracyMetrics`, `setHasPostProcessResult`, step jump, busy flag |
| Jobs → Export | write `m_lastClassifyPath` / post paths / accuracy panel (shared members) |
| Jobs → Core hub | read `m_rois`, source geo, class colors; apply consumes `m_loadedBackend` |
| Export → Sample edit | `loadRois` / project load → `syncSampleLayerFromRois` |
| Export → Jobs (indirect) | reads last paths only; no job launch |
| Export → Workflow | `setHasExportedOrLoadedToMain`; project restore sets many flags |
| Export → Core hub | ROI load mutates `m_rois`; project may `openSourceRaster` |
| JM/spectral → (none) | sinks only |

### 2.2 Setup order (ctor ~211–308)

```
m_rois + m_layerStore
m_canvas (central)
setupLayerManager          // session tree
setupMenus
setupToolbars              // sample + role actions (connections later)
setupDocks                 // class / JM / spectral
setupSampleVectorEditing   // tools + sample layer
setupClassifierBar         // binds apply/preview/CV + dock toggles
setupWorkflowUi            // controller + populateStepPanels (wires all)
setupStatusBar
connect m_rois::changed fan-out
restore session snapshot
syncWorkflowFromRois + refreshWorkflowUi
```

Chrome setup is sequential; runtime coupling is signal- and member-based, not layered.

---

## 3. Shared mutable state (leakage inventory)

### 3.1 Worst: dual truth sample layer ↔ ROI collection

| State | Writers | Readers |
|-------|---------|---------|
| `m_sampleLayer` features | digitize, delete, magic wand, `syncSampleLayerFromRois` | rebuild, canvas, select tool |
| `m_rois` geometries + pixel indices | `rebuildRoisFromSampleLayer`, `loadRois`, project load, class UI widgets | train, JM, spectral, workflow stats, export |
| `mSuppressSampleSync` | rebuild / syncSampleLayer | onSampleLayerEdited, rebuild entry |
| `mSuppressDirty` | save/load ROI paths | dirty mark on `m_rois::changed` |

**Why worst:** two representations of the same samples. Suppress flags prevent loops but are easy to get wrong. Pixel indices depend on `m_sourceGt`/W/H — opening a raster after digitizing re-rasterizes via rebuild; loading ROIs before a raster leaves empty indices until rasterize in load path.

**Seam today:** methods `rebuildRoisFromSampleLayer` / `syncSampleLayerFromRois` *are* the interface; ownership of “which is truth” is still split (edit path: layer truth; load path: ROI truth).

### 3.2 High: `m_rois::changed` fan-out (hub bus)

Ctor connects one signal to:

1. `recomputeSpectralCurves` (sync, GDAL open)
2. JM timer restart → `recomputeJmMatrix`
3. `mSession.markDirty` (unless `mSuppressDirty`)
4. `syncWorkflowFromRois`
5. `updateRoiStatusLabels`
6. `classDefChanged` → `applySampleLayerRenderer` + `syncWorkflowFromRois`

Any cluster that mutates `m_rois` pays for *all* side effects. Load paths must remember `mSuppressDirty`. No single coordinator object.

### 3.3 High: job finish lambdas as multi-cluster writes

`applyClassification` success lambda (~2326–2394) alone:

- clears busy (`setClassifyBusy` → workflow refresh)
- sets `m_lastClassifyPath`
- `m_workflow->setHasFullClassifyResult(true)`
- creates raster layer, sets **`m_previewLayer`**, `addSessionLayer`
- bridge `setClassifiedOutputArtifact`
- fills `m_accuracyPanel`, enables popup, `m_accuracySource = "holdout"`
- `setHasAccuracyMetrics`, jumps step to Accuracy
- `refreshWorkflowUi`

Similar multi-write patterns in preview (overwrites `m_previewLayer`) and post-process (paths + layer + workflow). **Jobs do not have a narrow result-apply interface**; the mainwindow is the transaction script.

### 3.4 Medium–high: path / result sinks

| Member | Set by | Used by |
|--------|--------|---------|
| `m_lastClassifyPath` | apply finish, project load | post dialog default, export step7, load-to-main, project save |
| `m_lastPostRasterPath` | post finish, project load | export, load-to-main preference, project |
| `m_lastPostVectorPath` | post finish, project load | export, project |
| `m_previewLayer` | apply, preview, post (raster) | preview removes previous; **semantic overload** |
| `m_loadedBackend` / `m_loadedScaler` | loadClassifierModel | apply (one-shot consume) |
| `mLastModelPath` | apply model-save dialog, snapshot | snapshot only |
| `m_accuracySource` | apply finish | project save/load only |
| `m_projectPath` | project save/load | dialogs |
| `mSession` last ROIs path + dirty | ROI I/O, closeEvent, snapshot | export defaults, dirty close |

### 3.5 Medium: source geo tuple

`m_sourceRasterPath`, `m_sourceWidth`, `m_sourceHeight`, `m_sourceBandCount`, `m_sourceGt[6]`, `m_sourceLayer` updated only in `openSourceRaster` (and project → open). Read by sample rasterize, train build, preview window, JM, spectral, magic wand. No struct/type; easy to desync if any writer is added later.

### 3.6 Medium: workflow dual authority

- **Controller** (`m_workflow`) — soft gates used by UI
- **Bridge** (`m_workflowBridge`) — runtime session step/complete + artifacts  

Comments in bridge header already mark dual-write as temporary. Mainwindow is the synchronizer (`currentStepChanged`, `completionChanged`, job finishes).

### 3.7 Lower (but real): train/valid role

`m_trainSampleRole` is UI chrome only (“验证样本” is a label; status bar notes ROI still shared). Workflow stats count *all* ROI pixels as training. Leakage is semantic, not multi-writer: sample role and train pixel gates are not actually connected.

### 3.8 Lower: `refreshWorkflowUi` as global paint

Touches stepper, step host gates, class/sample labels, enable/disable apply/preview/CV across toolbar + bar + step buttons, wizard dock visibility, status bar. Any cluster that mutates a gate-relevant flag must call it; callers are jobs, sample role, busy, evaluate/post skip, export, ROI sync path (via completionChanged partially).

---

## 4. Existing extracted pieces (already outside the mainwindow body)

These are **not** seams inside the cpp so much as **downstream modules** the mainwindow still orchestrates:

| Component | File(s) | Owned by mainwindow how |
|-----------|---------|-------------------------|
| Workflow controller | `rs_classify_workflow_controller.*` | `m_workflow` |
| Workflow bridge | `rs_classify_workflow_bridge.*` | `m_workflowBridge` |
| Session state | `rs_classify_session_state.*` | `mSession` |
| Stepper / step host | `rs_classify_stepper_bar.*`, `rs_classify_step_host.*` | chrome |
| Classifier bar | `rs_classifier_setup_bar.*` | params + signals |
| Class table / quick list | `rs_class_table_widget.*`, `rs_class_quick_list.*` | share `m_rois` |
| JM / spectral widgets | `rs_jm_matrix_widget.*`, `rs_spectral_curve_widget.*` | data set from recompute |
| Accuracy panel | `rs_accuracy_panel.*` | set from apply |
| Tasks | `rs_classification_task.*`, `rs_post_process_task.*`, (+ CV task) | constructed per job |
| ROI tools | `rs_roi_tool_*.*` | map tools |

**Implication:** much UI and compute is already modular; the **seams that still need inventory** are the mainwindow orchestration methods and the mutable members they share — not missing widgets.

---

## 5. Line-oriented method cluster index

Quick map for navigation of `qgsclassificationmainwindow.cpp`:

| Lines (approx) | Cluster |
|----------------|---------|
| 125–209 | anonymous helpers (SampleSelectTool, fitScalerOntoConfig) |
| 211–308 | ctor: hub + setup sequence + signal fan-out |
| 316–430 | session snapshot + ROI save + closeEvent (**export/session**) |
| 432–687 | shell menus/toolbars/docks/status (**chrome assembly**) |
| 571–632 | **session layer tree** |
| 689–1105 | **sample vector edit** (+ status labels) |
| 1107–1176 | classifier bar wiring (**jobs entry points**) |
| 1178–1906 | **workflow chrome** (setup, panels, sync, refresh) |
| 1908–1992 | source open (**core hub**) |
| 1994–2928 | **jobs** (train data, apply, preview, post, CV) |
| 2930–3175 | **JM / spectral** |
| 3177–3631 | **export / project** (+ model load feeds jobs) |

Header private method groups mirror the same seams (`qgsclassificationmainwindow.h` ~91–140, members ~142–233).

---

## 6. Leakage ranking (for later refactor prioritization — not a plan)

1. **Sample layer ↔ `m_rois` dual truth** + suppress flags  
2. **`m_rois::changed` multi-subscriber bus** (dirty / workflow / diagnostics / status / renderer)  
3. **Job finish lambdas** writing paths + layers + workflow + accuracy in one place  
4. **`m_previewLayer` semantic overload** (preview vs full result vs post)  
5. **Unstructured source geo fields** read by five clusters  
6. **Workflow controller vs bridge dual-write** (already documented in bridge)  
7. **`refreshWorkflowUi` global enablement** coupled to job busy + gates  

---

## 7. What this inventory is *not*

- No proposed class split, ownership rewrite, or file moves.  
- No judgment that any cluster is “ready to extract” without further design.  
- No change to runtime behavior.

**Deliverable for #2:** the six ticket clusters **do** already exist as shallow method/member modules; the mainwindow remains the orchestration shell; leakage is concentrated in dual sample representation, ROI change fan-out, and job-result application.

---

## Sources

- `src/app/classification/qgsclassificationmainwindow.h` (class layout, members)  
- `src/app/classification/qgsclassificationmainwindow.cpp` (method bodies, connections)  
- `src/app/classification/rs_classify_workflow_controller.h`  
- `src/app/classification/rs_classify_workflow_bridge.h`  
- `src/app/classification/rs_classify_session_state.h`  
- Sibling classification widgets/tasks under `src/app/classification/`
