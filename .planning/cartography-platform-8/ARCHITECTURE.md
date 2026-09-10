# ARCHITECTURE — cartography-platform-8

## Ownership invariants (unchanged)

Pi is the single agent loop; workflow runs through
WorkflowRunCoordinator → TaskCenter → JobEngine; `RSOperatorRegistry` is the
operator surface; models behind IModelRuntime; **QGIS is the only map
canvas/layout/rendering engine**; persistence stays in DatasetStore /
ExperimentStore; geospatial I/O lives in `src/geospatial/**`; publication
converges on the governed output/committer seams; UX state converges on
CommandRegistry/ContextRules.

This track adds no registry, cache, manager, runtime, store, scheduler,
protocol layer, or metadata vocabulary. Every milestone extends an existing
authoritative seam:

| Milestone | Seam extended |
| --- | --- |
| M1 NoData | `style_compiler.cpp` apply path (StyleSpec→QGIS renderer primitives) |
| M2 Locator connectors | `mapspec_compiler.cpp` inset loop + LayoutService `addItem("shape")` |
| M3 MapSpec v5 | `mapspec.{h,cpp}` envelope validation + `upgradeMapSpec` (additive, versioned) |
| M4 Page-aware solver | `composition.cpp` relative-constraint application + report |
| M5 Typography 2.0 | `typography.{h,cpp}` wrap engine (additive policies, defaults preserve v7 output) |
| M6 NoData legend QA | `quality.cpp` rule catalog + repair; compiler legend path |
| M7 Compose identity | `cartography_tools.cpp` compose response + `plan_tools.cpp` confirmation |
| M8 Evidence/docs | visual-regression methodology + doc drift fixes |

## Key design decisions (recorded per the autonomy contract)

1. **NoData semantics** (M1): `raster.nodata.value` (numeric) maps to
   `QgsRasterDataProvider::setUserNoDataValue(band, {QgsRasterRange(v,v)})`
   — the QGIS-native "additional no data values" mechanism; with
   `transparent: true` (default per QGIS semantics when nodata is set)
   pixels render transparent; `transparent: false` shades them with
   `QgsRasterRenderer::setNodataColor` (declared `color`, else the
   renderer's default). `label` is knowledge for legend/preflight (M6), not
   a renderer input. Applied for every raster renderer family (shared tail
   in `applyRasterBlock`) and mirrored in `buildRasterRenderer` so the
   knowledge path and the live path agree.
2. **Connector geometry** (M2): the connector is a straight polyline from
   the inset frame edge (nearest edge midpoint toward the target) to the
   page-space anchor of the target frame's projected extent center. The
   extent→page projection uses the same linear extent mapping QGIS applies
   (extent rect → map item rect); declared `style` (solid accent by
   default), `stroke_mm` (default 0.4), optional dash. Compiled through
   LayoutService `addItem` (shape) so ownership/provenance stay identical to
   every other item; deterministic ids `<inset-id>-locator-connector`.
3. **v5 output block** (M3): envelope-level `output: {formats?: ["png"|
   "pdf"], dpi?: 72..1200, dir?: string}` — declaration only; compilation
   does not auto-export (export stays an explicit governed action), but
   validation enforces the closed vocabulary/bounds and `cartography:compose`
   + `confirmMapOutput` surface the declaration so the harness gate can
   check it. Additive: absent block = v4 behavior; `upgradeMapSpec` stamps
   5 and normalizes nothing else.
4. **Page-aware solver evidence** (M4): the solver never invents geometry;
   when keep_with/avoid_overlap/inside would pin a companion past the
   follower's page bottom, the application is REJECTED with a decision
   record (`page_overflow` outcome naming the page height) and the
   constraint lands in `unsatisfied`/`violated` with core evidence —
   preflight's MAP_OFF_PAGE then cannot be triggered by solver output.
   Multi-page docs: the follower's own `page` index declares its page
   height. Bounded: one extra comparison per application, no extra passes.
5. **Hanging punctuation** (M5): `text.break_policy: "none" (default) |
   "push" | "pull" | "halfwidth"` implements standard CJK kinsoku
   remediation for lines that would end (or start) illegally: push the
   punctuation to the next line, pull it into the margin (hang), or render
   it halfwidth. Deterministic per-codepoint logic on the existing width
   model; default "none" reproduces 7.0 lines exactly.
6. **NoData legend** (M6): declarative QA + repair stamps
   `legend.nodata {label}`; the compiler renders the nodata entry as a
   sanctioned swatch composite (picture + label) inside the declared legend
   rect — same class of QGIS-backed furniture as charts/colorbars (4.0);
   documented honestly in limitations.md (QgsLayoutItemLegend cannot host
   custom nodes declaratively without leaving auto-update semantics).
7. **Compose identity** (M7): `cartography:compose` response gains
   `structural_digest` (existing `structuralDigest(spec)` over the RESOLVED
   spec) and `provenance {template, components[]}` (declared
   source_component/template fields, id-sorted, bounded); `confirmMapOutput`
   copies both into the confirmation so Harness identifies *what* was
   composed without a new metadata vocabulary (fields only).

## Cross-track dependencies

None in this milestone set. All edits are inside
`src/agent/{mapspec,cartography,harness}` + data/docs/tests owned by this
track (see OWNERSHIP.md). If a sibling 8.0 track lands changes to
`tests/CMakeLists.txt`, rebase resolves trivially (append-only test
registration).
