# OWNERSHIP — cartography-platform-8

Files this track owns (may create/modify) and the authoritative seams it
consumes without modification.

## Owned (implementation)

- `src/agent/cartography/style_compiler.{h,cpp}` — NoData → QGIS renderer
  wiring (G1).
- `src/agent/cartography/style_spec.{h,cpp}` — nodata label/report surface
  additions only (additive).
- `src/agent/mapspec/mapspec.{h,cpp}` — v5 additive envelope surfaces
  (`output`, deeper `binding` validation), `upgradeMapSpec`, validation.
- `src/agent/mapspec/mapspec_compiler.cpp` — locator connector compilation,
  legend nodata composite, output-block surfacing.
- `src/agent/cartography/composition.{h,cpp}` — page-aware relative
  constraint evidence (additive report fields + honest Blocked reasons).
- `src/agent/cartography/typography.{h,cpp}` — hanging-punctuation policy,
  declared line-height (additive; defaults preserve 7.0 outputs).
- `src/agent/cartography/quality.{h,cpp}` — MAP_NODATA_LEGEND rule +
  converging repair; rule catalog additions.
- `src/agent/cartography/chart_registry.cpp` — declared label budget
  (`style.max_label_chars`) in the QPainter render path (additive).
- `src/agent/cartography/cartography_tools.cpp` — compose report digest +
  provenance surfacing.
- `src/agent/harness/plan_tools.cpp` — `confirmMapOutput` carries the
  structural digest (narrow edit).
- `data/cartography/**` — only if a catalog addition is justified (index
  regenerated through the existing drift tooling).
- `tests/test_platform8.cpp` (new) + minimal touches to existing cartography
  tests where behavior intentionally evolves.
- `tests/CMakeLists.txt` — register the new test file in `test_mapspec`.
- Docs: `docs/cartography/{mapspec-reference,style-spec-reference,
  preflight-rules,limitations,visual-regression,migration-mapspec-v5}.md`.

## Consumed unmodified (authoritative)

- QGIS (renderer/layout/primitives) — the only rendering engine.
- `LayoutService` (`src/agent/layout_tools/`) — all item creation goes
  through `addItem`; no direct QgsLayoutItem insertion outside the compiler.
- `ComponentRegistry` / `TemplateRegistry` / `StyleRegistry` /
  `SolutionRegistry` / `ChartRegistry` / design tokens.
- Workflow stack (`WorkflowRunCoordinator → TaskCenter → JobEngine`), Pi.
- `DatasetStore` / `ExperimentStore`; governed output/committer seams.
- `CommandRegistry` / `ContextRules` workbench contracts.

## Cross-track seams (avoid)

- `src/geospatial/**`, `src/runtime/**`, `src/sdk/**` — owned by sibling
  8.0 tracks (geospatial-data-fabric-8, execution-plane-8,
  verification-platform-8). This track must not edit them; dependencies are
  consumed as-is.
- Shared files with elevated conflict risk: `tests/CMakeLists.txt` (all
  tracks add tests), `CHANGELOG.md`, `docs/adr/`. Keep edits append-only and
  minimal; rebase if a sibling lands first.
