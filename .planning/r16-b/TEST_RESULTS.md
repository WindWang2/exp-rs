# R16-B Test Results (integration branch `hardening/r16-b-scientific-decisions`)

All runs on the integration worktree, `dev-default` Debug build, `ctest -j1`, `QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib`. Baseline `origin/master` = `46791bc65`. Builds at `-j2` under the cross-prompt `flock` lease.

## RED evidence (pre-fix, track worktree `exp-rs-r16-b-t03`, master sources + new test only)

```
$ ctest -R test_science_context_broker
98% tests passed, 1 tests failed out of 40
The following tests FAILED:
	880 - test_science_context_broker::capability facts snapshot stays answerable across an authority reinstall (Failed) FAST
# case detail: assertions: 8 | 5 passed | 3 failed
#   CHECK( snapshot->authority == "capability_knowledge.first" )  -> answered "capability_knowledge.second"
#   CHECK( snapshot->entriesForIntent( "ndvi" ).size() == 1 )     -> 0 == 1
#   CHECK( snapshot->revision() == std::uint64_t{ 7 } ) with expansion: 9 == 7
```

## GREEN run 1 — integration branch, after cherry-picks AND round-1 review fixes

| ctest -R pattern | Result |
| --- | --- |
| `test_science_context\|test_preflight_authority_invalidation` | **100% tests passed, 0 failed, 102 tests** (broker 40, concurrency 14, goal_projection 13, live_authorities 15, authority-invalidation 20) |
| `^(test_preflight\|test_scientific_planner_core\|test_scientific_planner_constraints\|test_scientific_planner_golden\|test_scientific_planner_proposal\|test_suitability_core\|test_suitability_adversarial\|test_repair_planner_completion\|test_repair_planner_schema)` | **100% tests passed, 0 failed, 196 tests** (full preflight family incl. engine/rules/provider/golden/render/runtime_adapter/check_tool/report_schema, planner core/constraints/golden/proposal, suitability core/adversarial, repair completion/schema) |

Track-04 note: these 196 are the WP-T04-2 verification runs — the four modules' families pass against the integration branch that carries the T03 broker API change, proving no track-04 consumer of the seam regressed (grep had already shown `capabilityFacts()` has no consumer under `src/suitability|preflight|planner|repair_planner`).

## GREEN run 2 (post round-2 review, same commands)

(filled below after the second independent review round)

## Coverage boundaries, honestly stated

- ASan/TSan instrumented runs were NOT executed: the `sanitizer-debug` preset exists, but building the GDAL-linked closure under sanitizers against the host's local dependency prefix was not attempted in this round (time/lease budget shared with four sibling R16 tracks). The lifetime defect is covered by the deterministic sequential oracle (which failed 3 assertions pre-fix and passes post-fix) and the churn regressions; the remaining sanitizer gap is recorded as an environment limitation, not as a PASS.
- The churn tests are stress regressions: fixed iteration counts (2 threads × 300 calls each), no wall-clock dependence; they are not the RED oracle (the sequential test is).
