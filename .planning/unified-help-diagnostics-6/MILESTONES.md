# MILESTONES — Unified Help 6.0

Commit-aligned milestones; each ends with a compiling tree + focused tests.

## M-A Help ID & registry foundation → `feat(help): establish help descriptor registry and stable IDs`
- `src/help/`: help_id.{h,cpp} (grammar, validation, parameter derivation),
  help_descriptor.{h,cpp}, help_registry.{h,cpp} (duplicate/alias/reference checks),
  help_search_index.{h,cpp}, diagnostic_descriptor.{h,cpp}, diagnostic_catalog
  skeleton, help_content.qrc + `data/help/*.json` loader/validator.
- CMake target `sicnu_help` (Qt6::Core + jsoncpp, no app/operator linkage).
- Tests: id grammar, duplicate rejection, alias resolution, reference validation,
  index determinism.

## M-B/C Command help & availability facts → `feat(help): integrate command availability explanations`
- `command_catalog_source` interface; `CommandHelpProvider` merge (registry facts +
  commands.json knowledge).
- `availability_facts.{h,cpp}` view model + `ContextRules::explainFacts()` seam
  (rules remain in ContextRules).
- Workbench: `CommandRegistry::unavailabilityReason` now backed by facts projection;
  speckle ad-hoc lambda replaced by shared facts.
- Tests: facts for representative commands; help titles/summaries for all 34.

## M-D/E Operator & algorithm help → `feat(help): derive parameter help from operator schemas`
- `operator_catalog_source` interface; `OperatorHelpProvider` (schema-derived base +
  operator knowledge JSON).
- `data/help/operators/*.json` for all 105 `rs:*` (summary tier) + flagship deep
  parameter knowledge (sar_speckle, spectral_index, change_detection, temporal_*,
  terrain, classification, topographic_correction…).
- Tests: derivation from schema JSON; merge precedence; coverage of registry names
  (via a registry-stub fixture mirroring rs_operators_init).

## M-F Unified diagnostics → `feat(help): introduce unified diagnostic catalog`
- DiagnosticCatalog complete for harness/operator/geospatial/dataset/preflight
  families + `diagnostics.json` content.
- `resolve(family, code)` + fallback descriptor preserving original code.
- Tests: every harness/operator/geo code resolves; retrySense vs RetryClass drift.

## M-G/H/I GUI projections → `feat(app): add F1 context help and Help Center`
- `help_presenter.{h,cpp}` (tooltip/whatsThis/statusTip/disabled text assembly).
- `help_event_filter.{h,cpp}` F1 resolution (helpId property > objectName map >
  workbench fallback) — opens Help Center anchored.
- `help_center_dialog.{h,cpp}` — IA tree, search (index-backed), QTextBrowser
  rendering, related topics.
- Wiring: main_window (Help menu, F1 install), command palette disabled reason,
  schema form builder inline field help (unit/recommended tooltip), workbench
  empty-state guidance helper + first adoptions (classification, georef, layout,
  data manager).
- Tests: presenter string assembly; resolver mapping; dialog constructs headless.

## M-K CLI/agent projections → `feat(cli,agent): project bounded help metadata to CLI and Pi`
- CLI: `--help-topic`, `--list-topics`, `--operator-help <id>`, `--diagnostic <code>`.
- MCP: `get_tool_help` bounded summary (reuse compact-list pattern); tool manifests
  gain helpIds.
- Tests: budget-bounded summaries (char caps), CLI parsing.

## M-L/M Generated docs, drift & scale → `test(help): add coverage, drift and search-scale contracts`
- `help_markdown_writer` + export mode (CLI `--export-help-docs <dir>`); generated
  pages committed under `docs/generated/help/`.
- Drift/coverage tests: descriptors for all commands; parameter knowledge for
  schema-visible params of covered operators; no dangling refs; no duplicate ids;
  schema/help consistency; compact summary caps; search latency/build-time bounds.
- `COVERAGE_MATRIX.md` updated with measured numbers.

## M-N Adversarial review → review commits
- 2 subagents (architecture/GUI/search vs scientific/diagnostic/coverage).
- All P0/P1 + actionable P2 resolved; REVIEW_LOG.md updated.

## Final → `docs(help): document Unified Help System 6.0`
- FINAL_REPORT.md, PR body, docs/how-to-extend-help.md.
