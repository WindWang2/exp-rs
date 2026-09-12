# ADR 0146 — Unified bilingual terminology contract (RS glossary as single source)

* Status: Accepted (D6 · Chinese i18n & RS Glossary)
* Date: 2026-09-13
* Relates to: ADR 0133 (design tokens & a11y — typography/CJK fallbacks),
  Unified Help, Hint & Diagnostic Knowledge System 6.0 (`src/help/`)

## Context

Three consumers need the same remote-sensing vocabulary:

1. the **zh_CN translation layer** (`resources/translations/sicnu_zh_CN.ts`) — UI strings
   must use consistent, textbook-standard Chinese terms;
2. the **help system** (`src/help/`) — concept pages, search and tooltips should explain
   terms with the same wording students see in the UI;
3. the **agent (D9)** — answers quoted to students must use the same terminology.

Before D6 there was no translation pipeline at all, and Chinese explanations lived only
in hand-written help JSON. Terminology drift between UI, help and future agent answers
was unavoidable.

## Decision

`data/terms/rs_glossary.json` is the **single source of truth** for bilingual RS
terminology. The contract:

- **One entry per concept** with `en` (unique key), `zh`, `alias[]`, one-sentence
  classroom-usable `definition_zh`, one of 12 fixed `category` values, and `related[]`
  referencing other entries' `en` values. Schema: `data/terms/rs_glossary.schema.json`.
- **`sicnu::help::TerminologyProvider` is the only loader.** It validates entries
  (non-empty required fields, known category, unique `en`), exposes `lookup(en|zh|alias)`
  for tools and the D9 agent, and derives regular `concept.term.*` `HelpDescriptor`s via
  `appendDescriptors()`.
- **One catalog, no fork.** `composeHelpSystem()` appends the derived term descriptors to
  the existing `HelpRegistry`, so the help search index, Help Center and tooltips surface
  glossary terms through the pre-existing code path. No consumer re-implements glossary
  loading, and help content JSON does not duplicate glossary definitions.
- **Embedding follows the help content pattern**: the glossary ships inside
  `data/help/help_content.qrc` (as `:/help/terms/rs_glossary.json`), keeping `sicnu_help`
  free of filesystem/app-layer dependencies (layer guard respected: Qt Core + jsoncpp).
- **Translation consistency is by construction**: UI strings use English source text;
  the `.ts` supplies Chinese. Because the translation layer's zh values for RS terms are
  authored from the same glossary, UI, help and (later) agent answers agree.
- `.ts` is tracked; `.qm` is generated at build time (`sicnu_i18n`) and never committed.
  `lupdate` is an explicit maintenance target (`sicnu_i18n_update`), so ordinary builds
  never rewrite a tracked source file.

## Consequences

- Adding a term is a data change (`data/terms/rs_glossary.json`); help search, tooltips
  and agent lookups pick it up without code changes, and `tests/test_i18n` verifies
  resolution.
- The glossary grows monotonically: renames should keep `en` stable and move the old
  name into `alias[]` (mirroring the help id alias policy).
- Comparing displayed (`tr()`) strings remains safe only under the global invariant that
  identical English sources map to identical Chinese in every context; the translation
  tooling enforces this by mapping per source string, not per context.
- Out-of-scope sources (`src/agent`, `src/processing`, …) may still contain Chinese
  literals; extending the sweep later means widening the `sicnu_i18n_update` scan, not
  changing this contract.
