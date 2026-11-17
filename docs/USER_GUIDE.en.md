# SICNU GEO RS Studio · User Guide (English)

> Edition: R31-T03 · desktop workbench, cartography and bilingual experience.
> English counterpart of `USER_GUIDE.md` (中文). Both files ship together.
> **Machine identifiers, parameter names, units, diagnostic codes and
> availability facts are identical in both editions** and come from the
> installed help registry (`data/help/*.json`) and the application schema.
> User-defined names (project, layer, collection, output file) are never
> translated.

This is a task-oriented companion to the full Chinese manual, not a
replacement: the Chinese guide remains the complete twelve-chapter reference.
Where a described workflow has a known limitation, this guide says so
explicitly instead of promising a capability that is not present yet.

## 1. The ordinary desktop flow

The main window hosts the first-party desktop flow: open data, browse the
catalog, display one or two map views, run post-processing, compose a map and
export a deliverable. The command palette (`command.app.commandPalette`)
provides keyboard access to the same commands as the menus.

| Panel / action | Registry ID |
| --- | --- |
| Processing history | `command.workbench.processingHistory` |
| Temporal analysis workbench | `command.workbench.temporal` |
| Cartography workbench | `command.workbench.cartography` |
| Dual georeferencing / viewport | `command.workbench.georefDual` |
| Operator catalog | `command.workbench.operatorCatalog` |
| Visual analytics | `command.workbench.visualAnalytics` |
| Compose / preflight / repair / export | `command.cartography.compose`, `command.cartography.preflight`, `command.cartography.repair`, `command.cartography.export` |
| Workflow new / open / save / run / stop | `command.workflow.new`, `command.workflow.open`, `command.workflow.save`, `command.workflow.run`, `command.workflow.stop` |
| Retry / resume / mission timeline | `command.mission.task.retry`, `command.mission.task.resume`, `command.mission.timeline.show` |
| View linking | `command.view.linkCenter`, `command.view.linkScale`, `command.view.linkCursor`, `command.view.linkVisibility`, `command.view.linkUndo`, `command.view.linkGroupStatus`, `command.view.linkUnlinkAll` |

These IDs are stable. Searching an exact ID in the Help Center always resolves
to the single registered topic; the ID itself is never localized.

## 2. UI language and manuals

1. Open the language setting and choose `中文` or `English`.
2. **Restart the application.** The saved preference is applied at startup
   together with the application and Qt catalogs, which load as a pair.
   Changing the preference does not re-translate the running window.
3. If a catalog is missing, the application falls back to a complete English
   UI instead of showing a half-translated window.
4. `SICNU_LANG` overrides the saved preference for one launch; `system`
   restores system-locale behaviour.

The Help viewer resolves its manual from the **language actually in effect**
after catalog loading, not from the raw saved preference. If a catalog is
missing, the whole UI (including this manual) is English, so a window with an
English shell and Chinese prose can never occur:

| Effective UI language | Manual |
| --- | --- |
| `zh_CN` | `docs/USER_GUIDE.md` |
| `en` | `docs/USER_GUIDE.en.md` |

- If the requested manual is not installed, the other edition is shown
  **with a visible notice at the top**; the application never silently changes
  the language of the prose.
- If neither file is present, the built-in summary is shown with a notice.
  A manual that exists but cannot be opened also shows a visible notice.
- A relocated installation is supported via `SICNU_DATA_DIR`, or by setting
  `SICNU_GUIDE_DIR` to an explicit manual directory. **`SICNU_GUIDE_DIR` is
  authoritative**: when set, only that directory is searched, so a package
  missing one edition reports an honest fallback instead of silently picking
  a manual from the development source tree. The installed layout is
  `share/sicnu_geo_rs/docs/USER_GUIDE*.md`.

## 3. Data catalog, import and preview

The Data Manager lists assets and collections. A collection keeps a
`CollectionId` and an asset keeps an `AssetId`; the panel keeps selection and
expansion by those IDs rather than by row index, so a refresh or a rename does
not silently point a row at a different asset. Both standalone assets and
collection children are rendered with a bounded row budget, and the panel
states when more items exist so the filter can be used to narrow the search.

**Collection paging (R31-T03 WP02).** Each collection keeps its own page index,
keyed by `CollectionId` rather than by tree row. When members exceed the page
budget, two explicit action rows — *Previous page* / *Next page* — appear with
the exact visible range, total, page number and filter state. Select or
double-click an action row (or press Enter) to change page; the rows carry
distinct accessible names and are explicitly described as page-change actions,
not data assets. A collection-name filter hit shows the whole collection;
a child-name hit shows only matching children — both use the same filter rule.
Deleting the last member of the final page falls back to the new last page,
and the application can reveal a known tail `AssetId` directly. Rendering is
bounded by the page budget; the backend still supplies a full membership
snapshot, so this is a bounded UI, not an O(page) backend.

Import uses `command.layer.addRaster` / `command.layer.addVector` or the batch
import dialog. For a multi-subdataset container such as NetCDF, the preflight
report supplies the exact backend spellings
(`ImportPreflightReport.subdatasetNames` and `subdatasetDescriptions`).
Ordinary rasters/vectors, unsupported drivers and ambiguous product
recognition keep their own typed verdicts; the UI does not guess a choice.

**Known limitation (containers).** The batch review currently reports that a
selection is needed but does not yet expose a member picker that feeds the
original spelling back into `ImportSession`. A `NeedsSelection` verdict is
never auto-bypassed. This is tracked as R31-T03 WP06/WP07.

**Known limitation (previews).** Preview requests are bounded and use a
receiver-safe LRU, but a file replaced between request and delivery is not yet
identified by a request epoch. Treat a thumbnail of a file being overwritten
as needing a refresh. This is tracked as R31-T03 WP04/WP05.

## 4. Map views and cartography

Multiple map views are supported (`command.workbench.georefDual`). View linking
commands operate on the active view group; group zoom applies to the whole
active group, not just one canvas (R30 #2054 F1).

The cartography workbench separates the editable MapSpec draft, the check
report, the compiled project-owned layout and the submitted TaskCenter request.
Only a successfully compiled layout enables actions that depend on it. A
`compile=false` result does not create an "already compiled" state.

**Known limitation (revisions).** Editing a draft after a check does not yet
tag every dependent action with an explicit revision identity; verify that
Check / Repair / Produce were run after the last edit before exporting. This
is tracked as R31-T03 WP10/WP11.

Quality results are available as structured reports (severity, rule/code,
item/page, applied and still-reported fixes). Machine codes are shown
untranslated in both UI languages.

**Known limitation (preview pages).** The preview currently renders page 0 at a
fixed DPI. Multi-page and atlas browsing with a pre-render pixel budget is
tracked as R31-T03 WP13.

Export and produce results list format, path, page/atlas count, manifest and
checksums. An output conflict, missing write permission, cancellation or
partial failure leaves the previously published result intact.

**Known limitation (delivery inspection).** The results panel does not yet
offer open / reveal / copy actions with existence re-checks. This is tracked as
R31-T03 WP14.

## 5. Post-processing and cancellation

Post-processing (sieve, majority, clump) runs through the existing TaskCenter.
Requesting **Cancel** while a stage is running stops the task: it reports the
standard `Cancelled` terminal state, creates no new success card and registers
no half-product for a cancel detected **before the output write**; cancellation
arriving after a successful atomic write follows the existing post-save
contract. The normal, uncancelled path keeps the same numeric output.
Processing history uses the unified ActionPlan; a queued state is only shown
for a positive task ID, and a missing product is never paired with an
unrelated one (R30 #2054 F2).

Clump now forwards the task feedback to the backend, so cancellation is
observed inside the clump stage rather than only after the whole scan has
finished (R31-T03 WP16).

## 6. Help, diagnostics and stable identifiers

The Help Center groups commands, concepts, workbenches and diagnostics. Search
accepts keywords and exact IDs; a missing locale entry falls back to the
registered default without inventing content. Titles are escaped before
rendering, so text containing `<` cannot inject markup, and external links are
not opened implicitly.

Diagnostic codes are machine identifiers and stay verbatim, for example:

| Code | ID |
| --- | --- |
| `DATASET_NOT_FOUND` | `diagnostic.harness.dataset_not_found` |
| `BAND_ROLE_UNRESOLVED` | `diagnostic.harness.band_role_unresolved` |
| `CRS_MISMATCH` | `diagnostic.harness.crs_mismatch` |
| `GRID_MISMATCH` | `diagnostic.harness.grid_mismatch` |
| `INVALID_RADIOMETRY` | `diagnostic.harness.invalid_radiometry` |
| `INSUFFICIENT_MEMORY` | `diagnostic.harness.insufficient_memory` |

Concept IDs such as `concept.rs.ndvi`, `concept.dataset.provenance`,
`concept.rs.determinism` and `concept.rs.memory_policy` are identical in both
languages. The units and parameter names in the Chinese manual
(`Jeffries-Matusita`, `Kappa`, `DOS1`, `QUAC`, `GLCM`, `MeanShift`,
`Watershed`) are unchanged here.

**Known limitation (help knowledge locale).** Titles and prose in
`data/help/*.json` are currently Chinese for most topics. IDs, codes, units
and parameter names are locale-independent; full English knowledge payloads
are tracked as R31-T03 WP17.

## 7. Keyboard, accessibility and desktop layout

- Primary actions and results carry distinct accessible names.
- Focus returns to the invoking control after a dialog closes.
- Disabled controls state their reason.
- Normal actions do not open an extra modal dialog on every step.
- Automated behaviour tests run with the Qt offscreen platform; those results
  are recorded separately from native desktop verification (display scaling,
  theme and OS-native file dialogs).

Native desktop verification for narrow windows, 100%/150%/200% scaling and
dark/light themes is tracked as R31-T03 WP19 and is not claimed here.

---

*This manual documents the first-party desktop flow. Agent/Copilot sessions,
managed Python runtimes and the Pi SDK are separate product areas and are not
documented here.*
