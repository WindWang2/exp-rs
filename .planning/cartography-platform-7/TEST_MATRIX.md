# TEST MATRIX — Cartography Platform 7.0

Runner: `build-dev\test_mapspec.exe` (Catch2). Build: `-j2`. CTest: `-j1`.
Tags: existing `[mapspec] [cartography] [platform5] [platform6] [drift] [visual]
[determinism] [golden]` + new `[platform7]` with per-package subtags.

| Area | Case | Tag | Type | Gate |
| --- | --- | --- | --- | --- |
| Regression | all 6.0 suites stay green on branch point and after every milestone | `[platform6]` | unit | hard |
| A solver | hardness: hard violation blocks convergence report; soft violation downgrades score, never convergence | `[platform7][solver]` | unit | hard |
| A solver | priority: high-priority soft wins scarce space; deterministic tie-break (weight desc, id asc) | `[platform7][solver]` | unit | hard |
| A solver | permutation invariance: same constraint set, permuted declaration order → identical geometry | `[platform7][solver]` | unit | hard |
| A solver | unsat core: minimal conflicting set reported for known over-determined fixture | `[platform7][solver]` | unit | hard |
| A solver | budget: non-convergence stops at kMaxRelaxationPasses with violated list, no hang | `[platform7][solver]` | unit | hard |
| A solver | anchor authority generalized: anchor+constraint → constraint disabled, anchor_wins report | `[platform7][solver]` | unit | hard |
| A solver | collision: avoid_overlap resolves by priority order; unresolvable pairs reported | `[platform7][solver]` | unit | hard |
| B templates | multi-parent extends merge table (object merge / keyed array merge / facets union) | `[platform7][templates]` | unit | hard |
| B templates | cycle → rejected with problem; diamond inheritance resolves deterministically | `[platform7][templates]` | unit | hard |
| B templates | provenance: resolved descriptor records per-field source ids | `[platform7][templates]` | unit | hard |
| B templates | legacy single-extends + 56-template catalog byte-identical behavior | `[drift]` | unit | hard |
| C components | each new descriptor validates; wrong category/collection mapping fails | `[platform7][components]` | unit | hard |
| C components | applicability: component declined for wrong medium/sensor with reason | `[platform7][components]` | unit | hard |
| D typography | measurement known-answer: ASCII/CJK/mixed strings → expected widths (tolerance 1e-9) | `[platform7][typography]` | unit | hard |
| D typography | wrap: greedy + CJK punctuation rules; overflow → policy applied; report fields exact | `[platform7][typography]` | unit | hard |
| D typography | fitting: binary-search font within bounds; min-font floor respected; truncation flagged | `[platform7][typography]` | unit | hard |
| D typography | determinism: same input → byte-identical report across runs | `[platform7][typography][determinism]` | unit | hard |
| E style | applicability matrix: SAR/DEM/multiband accept/reject table | `[platform7][style]` | unit | hard |
| E style | diverging without center → rejected; categorical+interpolation → rejected | `[platform7][style]` | unit | hard |
| E style | contrast: below-floor text/class pairs warned; repair decision logged or rejection | `[platform7][style]` | unit | hard |
| E style | NoData wiring compiles to QGIS renderer on smoke fixture | `[platform7][style]` | unit | hard |
| F charts | dual axis without justification → rejected; with → accepted and rendered single-run deterministic | `[platform7][charts]` | unit | hard |
| F charts | accuracy_summary derives from inline confusion matrix; bounds respected | `[platform7][charts]` | unit | hard |
| G preflight | each new rule: violating fixture → issue with code/severity/suggested_action; repairable ones repaired within budget; residuals listed | `[platform7][preflight]` | unit | hard |
| G preflight | repair loop terminates; decision log complete | `[platform7][preflight]` | unit | hard |
| H visual | structural hash golden: known-answer layout → stable digest | `[platform7][visual]` | unit | hard |
| H visual | double-compile byte-equality incl. new surfaces | `[platform7][visual][determinism]` | unit | hard |
| H visual | PNG golden/determinism: run if crash RCA resolves; otherwise documented limitation (never claimed) | `[visual][golden]` | integration | best-effort+doc |
| I drift | positive: full catalog passes 5 ref-family checks | `[platform7][drift]` | unit | hard |
| I drift | negative (mutation): each family seeded with dangling ref → drift test fails | `[platform7][drift]` | unit | hard |
| Docs | docs drift test: reference docs cover new codes/kinds/fields | `[drift]` | unit | hard |
