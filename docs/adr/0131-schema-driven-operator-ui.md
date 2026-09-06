# ADR 0131: Schema-Driven Operator UI & Thin-Client Continuance (UX 4.0, Milestones B/C)

- Status: Accepted (Desktop Workbench & Unified UX 4.0 goal)
- Context: `SchemaFormBuilder` existed but had no validation, no preflight, and
  one consumer; meanwhile twelve-plus GUI flows ran real kernels as
  `callable:gdal_task` inline lambdas (band ratio, extract band, contrast
  stretch, image enhancement, fusion CLI) and the batch dialog routed `rs:`
  ids around `RSOperatorRegistry` via the `AtomicAlgorithmRegistry` adapter.
  Transport was clean (everything on TaskCenter) but identity was not —
  operator-level schema/logging/preflight were bypassed.
- Decision:
  1. **Schema form contract**: `SchemaFormBuilder` becomes the standard
     form-generation layer — descriptor/operator schema in (defaults, ranges,
     required, `x-ui-type` hints), validated editors out (required/range/
     minItems/color/JSON checks, inline `errorState` marks, summary line,
     `validationChanged` gating), advanced sections collapsed but reachable,
     accessible names from schema labels. Schema defaults are the single
     source of truth (rs:pca's dialog no longer invents `3`).
  2. **One processing surface**: `TaskPanelHost` is deepened into the standard
     run surface — form + validation gating + estimate line + run/stop +
     structured result (`RsResultSummary`).
  3. **Kernel promotion, not dialog deletion**: the dialog kernels moved
     verbatim into `src/processing/algorithms/band_tools.cpp` and the
     `rs:image_enhancement` operator; thin `rs:` JSON operators own execution
     (`rs:band_ratio` ratio|ihs, `rs:extract_bands`, `rs:contrast_stretch`,
     `rs:image_enhancement`); external CLI pan-sharpening became
     `otb:bundle_to_perfect_sensor` (OtbOperatorBase) and `gdal:pansharpen`.
     Numerics match the legacy paths (IHS additionally masks NoData to NaN —
     the enhancement panel's #380 semantics, now shared).
  4. **Batch respects the registry**: `rs:` ids in batch resolve through
     `RSOperatorRegistry::create` with a default context; the atomic-registry
     adapter contributes only its descriptor for parameter mapping.
  5. **Guardrail**: `test_ui_task_center_contract.cpp` scans all processing
     dialogs: no `runGdalTask(`, no `GDALOpen(`/`GDALRasterIO(`, submission
     only through the shared seam, batch executes through the operator
     registry.
  6. **Documented exceptions**: `module:classify:*` and `module:georef:*`
     remain TaskCenter-tracked interactive sessions (headless parity via
     existing `rs:` operators); `processing:` provider ids stay provider-owned.
- Consequences: GUI, CLI and MCP share one execution path for the migrated
  tools; the 2 GiB soft cap of the old band-ratio/extract lambdas is gone
  (streaming tiles); large-raster silent failures become typed errors; new
  dialogs cannot reintroduce inline kernels without a red test.
