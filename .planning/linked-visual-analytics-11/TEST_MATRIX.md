# TEST_MATRIX — linked-visual-analytics-11

All runs: `QT_QPA_PLATFORM=offscreen`, `ctest -C Debug -j1` in
`build-dev`. Evidence = exit code + the run's output tail in EVIDENCE.md.
Oracles are independent: expected cross-CRS values are computed from
EPSG definitions in the test (never via the code under test); hub/bounds
expectations are logical invariants.

| # | Capability under test | Independent oracle | Command | Result |
|---|---|---|---|---|
| T1 | extent propagation within group | peer canvas extent equals source extent after event-loop turn | `ctest -R test_view_link` | pending |
| T2 | cross-CRS extent known answer | 4326 rect → 3857 expected values from independent transform built in test | `ctest -R test_view_link` | pending |
| T3 | link groups isolation | view in group B unaffected by group A source | `ctest -R test_view_link` | pending |
| T4 | viewport history/undo | restore returns to recorded previous extent exactly | `ctest -R test_view_link` | pending |
| T5 | no infinite feedback loop (extent) | appliedSyncCount stops after one pass; echo events counted not propagated | `ctest -R test_view_link` | pending |
| T6 | cursor cross-CRS known answer | projected cursor equals independently computed transform of emitted point | `ctest -R test_view_link` | pending |
| T7 | view destroy during pending timer | no crash/UAF; QPointer guards (fast destroy) | `ctest -R test_view_link` | pending |
| T8 | hub echo suppression | re-entrant publish with same origin+generation dropped; cross-surface not | `ctest -R test_visual_analytics` | pending |
| T9 | hub generation monotonicity + bounded history | generations strictly increase; history ≤ 64 with correct eviction | `ctest -R test_visual_analytics` | pending |
| T10 | 100k logical selection events | 100k publishes dispatched; hub memory bounded (history cap); all subscribers get events; no growth | `ctest -R test_visual_analytics` (label [va][scale], bounded logical) | pending |
| T11 | brushing contract | hist range → scatter filtered set equals independently computed predicate | `ctest -R test_visual_analytics` | pending |
| T12 | stale-generation probe drop | superseded probe result never delivered; cancel stops work | `ctest -R test_visual_analytics` | pending |
| T13 | visibility link by AssetId | toggling visibility in view A flips view B same-asset layer; different-asset untouched; empty-asset never linked | `ctest -R test_view_link` | pending |
| T14 | layer removal / view removal purge | deleted layer stops linking; no dangling callbacks | `ctest -R test_view_link` | pending |
| T15 | view.* command availability | commands registered; disabled with reason when no views; checkable state follows controller flags (projected in test via registry API) | `ctest -R test_command_contract_9` (help↔registry coverage) + local availability check | pending |
| T16 | contract/hhelp drift gates | test_command_contract_9 + test_capability_drift pass | `ctest -R "test_command_contract_9|test_capability_drift"` | pending |
| T17 | regression: dual viewport untouched | test_dual_viewport_sync passes unchanged | `ctest -R test_dual_viewport_sync` | pending |

Scale note (T10): 100k events is a LOGICAL scale (tiny value objects through
the hub API in-process); no wall-clock assertion, bounded by the history cap
invariant. No opt-in env needed since it is logically bounded and fast.
