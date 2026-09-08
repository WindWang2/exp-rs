# TEST_MATRIX — Unified Help 6.0

All tests are deterministic, offline, and bounded (no network, no parallel full builds).

| Test target | File | Asserts |
| --- | --- | --- |
| Help ID grammar | `tests/test_help_ids.cpp` | valid/invalid ids per kind; parameter-id derivation; alias one-hop |
| Registry integrity | `tests/test_help_registry.cpp` | duplicate rejection, alias/deprecation, reference validation (related/diagnostics/docRefs), unknown-kind rejection |
| Search index | `tests/test_help_search.cpp` | deterministic ordering; CJK + EN queries; result cap; build < 200 ms for 2k synthetic topics; query < 5 ms (bounded loop) |
| Content JSON validation | `tests/test_help_content.cpp` | all `data/help/**` parse; ids exist as descriptors; no duplicate ids; no dangling relatedIds; no secrets (path/token regex scan); schema facts not restated (range duplication detector) |
| Command provider | `tests/test_help_commands.cpp` | merge of stub registry + JSON; every stub command gets descriptor; facts view model correctness |
| Operator provider | `tests/test_help_operators.cpp` | base tier from schema JSON (type/range/default/enum/required); knowledge merge; unit/recommended rendering; derivation for param ids |
| Diagnostic catalog | `tests/test_help_diagnostics.cpp` | all harness codes resolve; retrySense == RetryClass; operator + geospatial enums resolve; fallback preserves unknown code; remediation non-empty |
| Presenter strings | `tests/test_help_presenter.cpp` | tooltip ≤ bound; whatsThis rich; disabled explanation from facts |
| Markdown writer | `tests/test_help_markdown.cpp` | stable snapshot (golden) for sample registry; no hand-doc overwrites; id links valid |
| Coverage & drift | `tests/test_help_coverage.cpp` | every registered command has descriptor; every `rs:*` in stub operator list has descriptor; every parameter in flagship JSON matches a real schema param name; no orphan parameter.<op>.<param> ids |
| Compact projection | `tests/test_help_compact.cpp` | agent summary ≤ budget chars for all operators/commands; includes id/title/summary/params line |
| GUI helpers | `tests/test_help_gui.cpp` | HelpCenterDialog constructs; search returns; F1 resolver maps properties/objectNames (offscreen) |

Run pattern: build `sicnu_help` + test targets only (`ninja sicnu_help test_help_*`),
full-tree build once before review; `ctest -R help --output-on-failure`.
