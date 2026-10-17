# R16-B-T04 Performance / Budget Inventory (no code change — evidence track)

All figures read from master @46791bc65 (this branch adds no behavior change, so these remain the operative numbers).

## Hard caps already enforced in this track's modules

| Module | Cap | Value | Site |
| --- | --- | --- | --- |
| suitability | scene count per assessment | 1000 (`kMaxScenes`, typed refusal `suitability.too_many_scenes` above it) | `src/suitability/suitability_assessor.h:31` |
| planner | plan steps | 64 (`PlanLimits::kMaxSteps`) | `src/planner/planner_vocab.h:124` |
| planner | candidate plans per goal | 8 (`kMaxCandidates`; overflow demotes to a typed non-blocking question) | `src/planner/planner_vocab.h:125`, `planner_core.cpp:731-742` |
| planner | asset inputs per step | 8 (`kMaxInputsPerStep`; overflow = blocking question + verdict downgrade, never silent truncation) | `src/planner/planner_vocab.h:126`, `planner_core.cpp:204-225` |
| preflight | inputs per report | 64 (schema default when unset; dropped count surfaces as `SPF_BUDGET_EXCEEDED` Block) | `src/preflight/engine.cpp:240-247,316-323` |
| preflight | findings per report | 256 (same typed-truncation marker) | `src/preflight/engine.cpp:244,355-365` |
| preflight | registered rules | 512 (`RegistryFull` typed registration failure) | `src/preflight/engine.cpp:246,172-178`; regression `tests/test_preflight_engine.cpp:424` |
| preflight mirror | entries | 2048 (`kMaxEntries`, overflow = counted problem, fail-closed) | `src/preflight/capability_mirror.cpp:14,214-218` |
| preflight mirror | extends merge depth | 4 (`kMaxMergeDepth`; deeper resolved chains refused, cycles refused) | `src/preflight/capability_mirror.cpp:13`; regressions `tests/test_preflight_provider.cpp:452/486/557/711` |
| suitability grid | pairwise comparison cost | linear-pair decode + bounded sampling, cross-checked against brute force at n=12/n=21 | `tests/test_suitability_adversarial.cpp:1127-1263` |

## Measured runs on this branch

Verification runs of the track's test families execute on the R16-B integration branch (they exercise the same module sources; this branch changes no code) — results recorded in `.planning/r16-b/TEST_RESULTS.md`.

## Position on optimization

No performance defect was demonstrated (no slow path reported, no regression measured); per the task's rules, algorithmic "improvements" without a measured problem are not landed. The caps table above is the audit deliverable.
