# HELP SURFACE INVENTORY — Unified Help 6.0

Every existing surface that carries help-like text, with its owner and current gaps.

## A. Command surface (`src/app/workbench/`)

- `command_registry.h` — `CommandDefinition{id,title,description,iconName,shortcut,
  category,keywords,checkable,destructive,availability,checkedState,explain,handler}`.
  34 commands registered in `command_defs.cpp` (project×8, layer×8, map×10,
  workbench×4, rs tools×17 incl. speckle/extractBands).
- Gaps: `description` is a single sentence; no purpose/prerequisites/related; `explain`
  yields flat strings; keywords only feed palette search.

## B. Availability rules (`selection_context.*`)

- `ContextRules` predicates (rasterSelected, vectorSelected, editingActive,
  editingAvailable, sarSelected, layerSelected, resultSelected, assetSelected,
  unavailabilityReason).
- Gaps: reason is a single string; no fact list (✓/✗); suggested action lives in
  caller heads only.

## C. Operator metadata (`src/operators/`)

- `rs_operator.h` — name/displayName/group/description/schema/metadata/
  determinism/memoryPolicy/executionEstimate; 105 `rs:*` registered.
- `rs_schema.h` — param builders carry name/description/default/range/enum/required.
- `metadata()` — purpose/prerequisites/limitations/workflowHints/tags present on
  several SAR/temporal operators; inconsistent across the 105.
- Gaps: units almost never declared; no recommended values; parameter scientific
  meaning usually absent; no per-parameter help ID; no requirement that a parameter
  description exists.

## D. Error/diagnostic surfaces

- `rs_operator_error.h` — 24 codes; messages at throw sites; details JSON; no
  remediation text, no "why it matters".
- `geospatial/common.h` — GeoError, 22 codes; message only.
- `harness_error.h` — 24 stable codes + category + RetryClass + suggestedActions
  (machine-oriented {action, arguments}); no human remediation catalog.
- `scientific_preflight.h` — checks `{check, passed, severity, code, details}`;
  codes not catalogued anywhere user-facing.
- `dataset_quality.h` — `LabelQualityFinding.code` (e.g. `label.unknown_class`),
  `LeakageFinding.kind/severity`; `dataset composition` findings.
- Gaps: four separate vocabularies with no unified lookup; severity/retry semantics
  duplicated or implicit; remediation prose scattered in docs only.

## E. Form/UI surfaces

- `schema_form_builder.h` — builds fields from schema; labels = param name +
  description; validation inline. No unit display, no recommendation hover, no
  F1/help link per field.
- 304 `setToolTip` / 35 `setWhatsThis` call sites in `src/app/**` — hand-written,
  divergent, untestable.

## F. Processing toolbox help

- `processing/algorithm_help_catalog.*` — shortDescription/shortHelpString keyed by
  algorithm name (GDAL/OTB wrappers + generic CLI). Separate content pool; goal is
  to link via help IDs for wrapper algorithms rather than merge narratives.

## G. Agent surfaces

- `mcp_server.h` — compact tools/list (names+descriptions), `get_tool_schema`.
- `harness/tool_manifest.*` — ToolManifest per tool id.
- `harness/recipe_catalog.*` — solution/recipe JSON (templates).
- Gaps: no deeper help pointer; no token-bounded help summary; risk/side effects only
  in manifests.

## H. CLI

- `main_cli.cpp` — global QCommandLineParser (pipeline/list/schema/catalog options);
  subcommands carry own parsers. `--help` is generic; no operator-level help; no
  diagnostic ID lookup.

## I. Docs

- `docs/**` — 402 md files (narrative). Not linked to code facts; drifts silently.
  Generation target: `docs/generated/help/**` for command/parameter/diagnostic/
  shortcut references.
