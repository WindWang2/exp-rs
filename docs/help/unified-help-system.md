# Unified Help, Hint & Diagnostic Knowledge System 6.0

One authoritative, machine-readable help layer drives every help-like surface:
GUI tooltips, What's This, status tips, disabled-control explanations, inline
parameter help, workbench empty states, the Help Center, F1 context help, CLI
help, MCP/Pi compact tool help, and the generated Markdown reference.

## Design invariants

1. **Help never owns business logic.** Availability rules stay in
   `ContextRules`, validation stays in operator schemas, error codes stay in
   their origin taxonomies. The help layer (`src/help`, `Sicnu::help`) links
   only Qt Core + `Sicnu::data` + jsoncpp and reads other registries through
   narrow source interfaces (`help_catalog_source.h`).
2. **Facts owned elsewhere are derived, never retyped.** Parameter type /
   range / default / enum are read from the operator schema at composition;
   shortcuts from the CommandRegistry; retry classes from `harness_error`.
   Drift tests fail the build if content re-states machine facts or invents
   parameters the schema does not declare.
3. **All prose lives in `data/help/*.json`** (embedded via `help_content.qrc`
   compiled into executables; see note on static-lib RCC). C++ contains no
   user-facing help text. Chinese-first content, English stable IDs.
4. **Bounded resources.** Deterministic search index (token/bigram inverted
   index, capped results), O(1) id lookup, char-capped agent summaries
   (`HelpCompact`), no network, no Markdown parsing on hover.

## Help ID scheme

```
command.<registry id>            command.layer.toggleEditing
operator.<id with : → .>         operator.rs.sar_speckle
parameter.<operator>.<param>     parameter.rs.sar_speckle.kernelSize
workbench.<name>                 workbench.classify
diagnostic.<family>.<code>       diagnostic.harness.dataset_not_found
concept.<domain>.<name>          concept.dataset.spatial_leakage
template.<domain>.<name>         template.report.scientific
shortcut.<name>                  shortcut.map.zoomIn
```

Segments are `[A-Za-z0-9_]`, case-sensitive, verbatim from the owning
registry. Diagnostic codes normalize SCREAMING/CamelCase to snake_case.
Aliases (`aliasOf`) give deprecated ids a one-hop redirect.

## Composition

`composeHelpSystem()` (startup, idempotent):
embedded JSON knowledge → derived command descriptors (CommandRegistry) →
derived operator + parameter descriptors (RSOperatorRegistry via
`Sicnu::help_adapters`) → reference validation. The GUI does this in
`HelpSystemController::compose`; CLI/MCP reuse the same call.

## Surfaces

| Surface | Entry point |
| --- | --- |
| Tooltip / status tip / What's This | `HelpPresenter` via `HelpSystemController::bindAction/bindWidget` |
| Disabled command explanation | `AvailabilityFactsAdapter` (facts = ContextRules predicates) → tooltip suffix + `explainAvailability` |
| Inline parameter help | `SchemaFormBuilder::setHelpContext(op id)` — unit/recommended/trade-off on each field's tooltip, helpId property for F1 |
| F1 context help | `HelpEventFilter`: widget `helpId` property → objectName map → active workbench → Help Center |
| Help Center | `HelpCenterDialog`: search + category tree + structured topic pages (`helpid://` links) |
| CLI | `--operator-help <id>`, `--help-topic <id>`, `--list-topics`, `--export-help-docs <dir>` |
| MCP/Pi | `get_tool_help` tool: ≤220-char summary + key parameters + limitations + diagnostics |
| Generated docs | `HelpMarkdownWriter` → `docs/generated/help/**` (committed, byte-stable) |
| Empty-state guidance | `WorkbenchGuidance` widget fed by `GuidanceDescriptor` |

## Extending

1. New command: register in `CommandRegistry` as today; add a
   `command.*` entry to `data/help/commands.json` (purpose, prerequisites,
   suggested next action, related). The coverage test fails if you forget it.
2. New operator: implement the operator; add a summary entry under
   `data/help/operators/<family>.json`. Schema parameters are covered
   automatically; add curated `parameters:` entries for units/recommendations.
3. New error: add the code to its origin taxonomy, then a `diagnostic.*`
   entry in `data/help/diagnostics.json` — the id must equal the derived
   mapping (checked at load), remediation first bullet = cheapest safe fix.
4. UI widget: `setProperty("helpId", "<topic>")` for F1, or
   `HelpSystemController::instance().bindWidget(widget, topic)`.

## Testing

- `test_help_core` — grammar, registry, search determinism + scale bounds,
  content validation, providers, availability facts, presenter bounds,
  compact budgets, diagnostics, markdown stability (fast, Qt Core only).
- `test_help_coverage` — live-registry coverage/drift: every `rs:*` operator
  and every schema parameter covered; command source-scan vs knowledge;
  harness/operator error codes resolve with matching retry sense; secret
  scan; markdown completeness.
