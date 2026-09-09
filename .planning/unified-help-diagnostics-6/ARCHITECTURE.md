# ARCHITECTURE — Unified Help, Hint & Diagnostic Knowledge System 6.0

## Layering (dependency direction ↓)

```text
┌──────────────────────────── surfaces ────────────────────────────┐
│ GUI (tooltips/What's This/status/disabled-reason/F1/HelpCenter)  │
│ CLI help · MCP/Pi compact summaries · generated Markdown         │
└──────────────────────────────┬───────────────────────────────────┘
                               │ projections only
┌──────────────────────────────▼───────────────────────────────────┐
│ src/help  (sicnu_help; Qt6::Core + jsoncpp only)                 │
│  HelpId · HelpDescriptor · HelpRegistry · HelpSearchIndex        │
│  DiagnosticCatalog · HelpPresenter(helpers) · HelpMarkdown       │
│  providers: CommandHelpProvider · OperatorHelpProvider           │
│            (read-only seams over existing registries)            │
└───────┬───────────────────────────────┬──────────────────────────┘
        │ additive knowledge            │ derives facts
┌───────▼──────────────┐   ┌────────────▼──────────────────────────┐
│ data/help/*.json     │   │ authoritative sources (NOT modified   │
│ (qrc-embedded)       │   │ beyond narrow read-only seams):       │
│  commands.json       │   │  CommandRegistry / ContextRules       │
│  operators/*.json    │   │  RSOperator::schema()/metadata()      │
│  diagnostics.json    │   │  HarnessError / GeoError /            │
│  concepts.json       │   │  RSOperatorError / dataset findings / │
│  workbenches.json    │   │  preflight checks                     │
└──────────────────────┘   └───────────────────────────────────────┘
```

Anti-goal guard: the help layer **reads** ContextRules/schemas; it never decides
availability, never validates parameters, never changes error codes. Circular
dependencies are prevented because `sicnu_help` does not link `sicnu_app`/`sicnu_operators`;
providers are thin adapters that copy facts at query time (registered into the help
library via small interfaces defined *in* sicnu_help).

## Help ID scheme (v1, fixed grammar)

```
kind        := command | operator | parameter | workbench | diagnostic | concept | template | shortcut
id          := kind "." domain "." name        (dot-separated, [a-z0-9_], stable)
examples:
  command.layer.toggleEditing
  operator.rs.sar_speckle
  parameter.rs.sar_speckle.kernelSize
  workbench.classification
  diagnostic.harness.dataset_not_found
  diagnostic.rs.sar.geometry.missing_look_direction
  concept.dataset.spatial_leakage
  template.report.scientific
```

Rules: IDs are English, immutable once released; display text is 中文-first.
`parameter.<operator-id-with-dots-replaced>.<param>` derives from operator IDs.
Deprecated IDs get `aliases →` new ID; registry keeps alias resolution one level deep.

## HelpDescriptor (composition, not one giant struct)

Core record + kind-specific payloads composed by value:

- `HelpDescriptor { id, kind, title, summary(≤120 chars), category, keywords[],
    relatedIds[], diagnosticIds[], docRefs[], deprecated, supersededBy }`
- `CommandHelp { purpose, prerequisites[], suggestedNextAction, relatedCommandIds[] }`
- `ParameterHelp { unit, meaning, recommended, tradeOff, warnings[], performanceNote,
    dependsOn[] }` — *attached* to schema-derived facts (type/range/default/enum come
    from the operator schema at query time, never stored).
- `AlgorithmPage { whatItDoes, whenToUse, inputs[], outputs[], assumptions[],
    unitsDomain, keyParameters[], limitations[], failureModes[], relatedTools[] }`
- `DiagnosticDescriptor { code (original machine code preserved), whatHappened,
    whyItMatters, severity, retrySense(none|manual|transient|derived), remediation[],
    relatedHelpIds[], technicalNote }`
- `GuidanceDescriptor { emptyStateId, headline, body, actionCommandIds[],
    helpTopicId }` for workbench empty states.

Availability facts (Milestone C) are a *view model* built by the GUI adapter from
`SelectionContextSnapshot` + `ContextRules` (which stay authoritative):
`AvailabilityFact { label, satisfied }` + suggested command id. The help layer defines
the fact struct and presentation; ContextRules keeps owning rule logic (a small
`explainFacts(snapshot, commandId)` seam is added next to the existing
`unavailabilityReason`, so rules remain in one place).

## Providers (derivation, read-only seams)

- `CommandHelpProvider`: queries `CommandRegistry::definitions()` (via a plain
  interface `CommandCatalogSource` to avoid linking app into CLI tools) and merges
  with `data/help/commands.json` additive knowledge.
- `OperatorHelpProvider`: queries `RSOperatorRegistry` (same interface pattern:
  `OperatorCatalogSource`) → schema-derived parameter facts + `data/help/operators/*.json`
  knowledge. Parameter Help IDs are derived (`parameter.rs.sar_speckle.kernelSize`),
  so coverage is checkable mechanically.
- `DiagnosticCatalog`: static mapping tables (harness codes, operator ErrorCodes,
  GeoError codes, dataset finding codes, preflight issue codes) → DiagnosticDescriptor.
  `resolve(originFamily, code)` keeps the original code untouched.

## Search (Milestone M bounds)

`HelpSearchIndex`: inverted token index built once from registry (title+keywords+
summary tokens; CJK bigram for Chinese terms). Deterministic ordering (score, then id).
Query latency target <5 ms for 2k topics at n=20 results; index build <50 ms; lookup
by id O(1) hash. No network, no Markdown parsing on hover.

## Projections

- `HelpPresenter` (GUI helpers): `tooltip(desc)` concise (title + summary, ≤~90 chars),
  `whatsThis(desc)` richer, `statusTip(desc)`, `disabledExplanation(facts)`.
- F1: `HelpEventFilter` (QApplication-level) maps focused object → HelpId via a
  `HelpContextResolver` (dynamic property `helpId` on widgets > objectName maps >
  workbench fallback) → opens Help Center anchored at topic.
- Help Center: `HelpCenterDialog` — tree of categories (IA per goal) + search box +
  `QTextBrowser` rendering structured descriptors to HTML locally.
- CLI: `--help-topic <id>`, `--list-topics`, richer `--operator-help <id>`.
- MCP/Pi: bounded `helpSummary(id, budgetChars)`; tool manifests link help IDs.
- Markdown exporter: renders command/parameter/diagnostic/shortcut references into
  `docs/generated/help/**` (checked in, drift-tested).

## Testing strategy

Pure-Qt Core tests (no widgets): registry/id/alias/reference validation; derivation
from sample schemas; provider merging; search determinism + latency bounds; markdown
export snapshot; drift tests (every registered command/operator has a descriptor;
no dangling related; no duplicate ids; schema-vs-help consistency); compact summary
token bounds. GUI widget tests limited to presenter string assembly (no needed
platform window).

## Data-driven knowledge format

`data/help/*.json` compiled into the `sicnu_help` Qt resource (`help_content.qrc`),
validated at startup (schema check in tests) and by a drift test that enumerates JSON
files — unknown Help IDs or malformed entries fail tests. Chinese content lives here;
C++ contains zero user-facing help prose except fall-back "未收录" placeholders.
