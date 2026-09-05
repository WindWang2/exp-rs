# ADR 0127: MapSpec — Declarative Cartographic Document

- Status: Accepted (implemented in this branch)
- Context: the Cartographic Layout Studio exposes item-level `layout:*` tools. An
  autonomous planner (Pi) cannot reason in item ids and coordinates alone; it needs a
  versioned, validated, agent-editable semantic document for map products.

## Decision

Introduce **MapSpec** (`src/agent/mapspec/`), a JSON document that describes a map
product as semantic collections (`map_frames`, `titles`, `legends`, `scale_bars`,
`north_arrows`, `charts`, `colorbars`, `grids`, `annotations`, `source_notes`,
`constraints`, …) with stable item ids and optional `semantic_role`s.

- MapSpec is **compiled** to `QgsPrintLayout` through `LayoutService` item factories
  (`MapSpecCompiler::compile`) so the compiled layout is byte-identical to what the
  Layout Designer / `layout:*` tools would produce — QGIS stays the single rendering
  and layout engine. No parallel renderer is introduced.
- Layouts can be **extracted** back to MapSpec (`MapSpecCompiler::extract`); the
  roundtrip is best-effort and non-mappable QGIS items surface as annotations with a
  `qgis_type` marker (documented divergence).
- Documents are versioned (`spec_version`), strictly validated
  (`validateMapSpec`: envelope, ids, geometry, references) and migrated
  (`upgradeMapSpec` v0→v1). Agents patch documents with add/update/remove ops
  (`applyMapSpecPatch`), never by resending whole documents.
- Persistence: the MapSpec travels with the layout (layout name is the join key);
  re-compiling a MapSpec replaces its layout deterministically.

## Consequences

- The compose → inspect → preflight → repair → export loop operates on one document
  type that Pi can read and patch (see ADR 0128 and `cartography:*` tools).
- Component/template libraries (`data/cartography/…`) produce MapSpecs, not QGIS
  templates; `.qpt` template save/load remains available through `layout:*`.
