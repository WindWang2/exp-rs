# MILESTONES — Cartography Platform 7.0

Each milestone: contract first (schema + validation + docs), then behavior, then
tests, then commit. No milestone breaks the previous one's tests.

| ID | Milestone | Packages | Verification gate |
| --- | --- | --- | --- |
| M0 | Planning archive + baseline build green | — | test_mapspec `[platform5],[platform6]` + `[mapspec],[cartography],[drift]` pass on branch point |
| M1 | Solver 7.0: hardness/priority/weight, multi-fixpoint policy, unsat core, budget, decisions log; MapSpec v4 constraint surface; docs | A | new `[platform7][solver]` tests: priority ordering, soft violation accounting, unsat core minimality (small cases), anchor authority, budget stop, permutation determinism; all 6.0 tests green |
| M2 | Template inheritance v4: multi-parent extends, facets merge, variants deep-merge, provenance, cycle detection; migration of provable duplicates | B | `[platform7][templates]`: merge semantics table-driven tests, cycle rejection, provenance report, catalog still loads + drift green |
| M3 | Component system: 14+ descriptors w/ role/bounds/tokens/applicability; registry validation growth | C | `[platform7][components]`: schema validation, collection mapping, applicability; drift: every template's components resolve |
| M4 | Typography engine: measurement model, wrap, truncation policy, fitting, fit report; preflight hooks (tiny text, clipped title) | D | `[platform7][typography]`: deterministic known-answer wrap/fit cases incl. CJK; cross-run byte-stable report |
| M5 | Style semantics: scheme/nodata/uncertainty/modality packs/ontology/contrast; reject-or-repair behavior | E | `[platform7][style]`: applicability accept/reject matrix, contrast thresholds, deterministic repair decisions; compiler application where QGIS primitives exist |
| M6 | Charts: accuracy_summary/time_series/class_composition kinds; dual-axis justification policy; placement contract | F | `[platform7][charts]`: validation matrix incl. dual-axis rejection, renderer smoke via inline path |
| M7 | QA/preflight growth: 9 new rule codes + bounded repair + decision log | G | `[platform7][preflight]`: per-rule fixture (violating spec → issue → repair or residual), rule catalog docs update |
| M8 | Visual regression: structural-hash goldens, compile determinism extension, headless PNG crash root-cause or honest documented alternative | H | `[platform7][visual]` structural/determinism cases green locally; crash RCA documented in limitations.md/REVIEW_LOG |
| M9 | Knowledge drift closure: 5 ref-family checks + index regeneration | I | `[platform7][drift]`: mutation tests (introduce dangling ref → drift fails) |
| M10 | Docs (mapspec-reference, preflight-rules, template-authoring, style-spec-reference, chart docs, migration-v4, gallery), BASELINE/TEST_MATRIX final, full local regression | — | whole test_mapspec + harness evals green; docs drift test green |
| M11 | Adversarial review (≤2 subagents, read-only), remediation of P0/P1 + actionable P2, integration sync with master, PR | — | REVIEW_LOG dispositions; clean merge onto latest master; PR opened |

## Sequencing rationale

Solver (M1) is first: M2/M7 consume its explanation surfaces. Typography (M4)
precedes preflight growth (M7) which consumes fit reports. Drift (M9) lands after
catalog-changing milestones (M2/M3/M5/M6). Docs (M10) last before review so they
describe shipped behavior.
