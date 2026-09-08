# GOAL — Unified Help, Hint & Diagnostic Knowledge System 6.0

One authoritative, machine-readable help/diagnostic knowledge layer for exp-rs that drives
GUI tooltips, What's This, disabled-control explanations, inline parameter help, workbench
guidance, Help Center search, F1 help, CLI help, MCP/Pi compact tool explanations,
diagnostic remediation, and generated Markdown reference.

## Non-negotiables

- Help metadata is *never* a second business-logic engine. Availability rules stay in
  `ContextRules`; validation stays in operator schemas; error codes stay in their origin
  taxonomies. Help only references and presents.
- Facts owned elsewhere (parameter range/default/enum, shortcut, availability) are
  **derived** from the authoritative source at runtime — never retyped into help content.
- Additive human/scientific knowledge (purpose, recommended values, trade-offs,
  remediation steps) lives in a dedicated content layer (`data/help/**`) keyed by stable
  Help IDs.
- Stable, versioned Help ID contract: `<kind>.<domain>.<name>` (see ARCHITECTURE.md).
- Chinese-first user text (current UI language), English stable IDs.
- Bounded resources: cheap hover lookups, deterministic search, token-bounded agent
  summaries, -j1/-j2 builds.

## Surfaces to serve

| Surface | Owner projection |
| --- | --- |
| GUI tooltip / status tip / What's This | `HelpPresenter` helpers |
| Disabled-control explanation | availability facts from `SelectionContextSnapshot` + `ContextRules` |
| Inline parameter help | `SchemaFormBuilder` field help from operator schema + knowledge |
| Workbench / empty-state guidance | guidance descriptors + panel integration |
| Help Center + search | `HelpCenterDialog` over `HelpSearchIndex` |
| F1 context help | `HelpEventFilter` widget→HelpId resolution |
| CLI help | `sicnu_cli` help projections |
| MCP/Pi compact help | bounded summary projection |
| Generated Markdown | `docs/generated/help/**` exporter + drift tests |
| Diagnostics | `DiagnosticCatalog` mapping origin error codes → remediation |

## Completion criteria

See the goal definition; summarized: stable tested registry; commands explain
availability; parameters expose unit/range/default/meaning; diagnostics actionable;
F1 resolves; Help Center local search; CLI/Pi reuse metadata bounded; generated docs do
not duplicate authoritative facts; coverage/drift tests; no business logic in help; no
secrets; no unresolved P0/P1 at final review; local validation evidence recorded.
