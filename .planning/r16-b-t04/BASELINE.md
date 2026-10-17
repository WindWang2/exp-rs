# R16-B-T04 Baseline — Suitability / Preflight / Planner / Repair

- Baseline SHA: `46791bc65c6f552ebbcf723bb91892736d56f319` (`origin/master`).
- Worktree: `/home/kevin/project/exp-rs-r16-b-t04`, branch `hardening/r16-b-t04-decision-convergence`.
- Ownership: `src/suitability/**`, `src/preflight/**`, `src/planner/**`, `src/repair_planner/**` (+ track-named test files if needed; none were needed — see below).

## Full-audit result (every lead re-verified on master source before any conclusion)

Files read end-to-end: `suitability_goal.cpp` (405), `suitability_assessor.cpp` (85), `preflight/engine.cpp` (385), `preflight/capability_mirror.cpp` (464), `planner/planner_core.cpp` (811), `planner/planner_constraints.cpp` (119), `planner/planning_context.cpp` (guarded-parse region), `repair_planner/repair_policy.cpp` (91). Pattern scans (untyped `asInt/asBool/asString` without preceding `isX()` guard; unbounded loops; silent defaults) across all four modules.

| Lead | Where | State on master @46791bc65 |
| --- | --- | --- |
| #1466 P1 integral fail-open | `suitability_goal.cpp:55-72` `boundedIntegral` | **fixed** — wrong type → typed `suitability.goal_invalid` failure; ±2^53 range gate; regression: `tests/test_suitability_adversarial.cpp:846` ("unrepresentable integral goal fields from json fail typed"), `:1567`, `:1404` (facts side) |
| #1450 canonicalizer dedup | `suitability_goal.cpp:335-344` `contentDigest` | **fixed** — RFC 8785 single digest authority, inline #1450 comment |
| #1447 `capability_mirror.cpp:23` parse throw | `preflight/capability_mirror.cpp:16-37` `parseText` | **fixed** — try/catch + `stackLimit`, inline #1450 comment |
| determinism oracle | golden + adversarial suites | **covered** — `test_preflight_golden.cpp:312` (full-matrix byte-identical replay), `test_suitability_adversarial.cpp:1071` (byte-identical reports), `test_scientific_planner_golden.cpp:133` (byte-stable offline replay) |
| fail-closed oracle | adversarial suites | **covered** — `test_suitability_adversarial.cpp:813/846/917` (hostile magnitudes, wrong-typed fields, garbage reports), `test_preflight_golden.cpp:232` (typed unknowns never a pass) |
| rule-conflict termination | mirror extends chains | **covered** — `test_preflight_provider.cpp:452/486/557/711` (deep chains, forward refs, cycles) |
| course-scenario golden matrix | preflight golden | **covered** ≥9 scenarios: missing nir band, CRS mismatch, coarse resolution ratio, undeclared operator, mirror outage, typed unknowns, temporal shortfall, train/eval leakage, model family mismatch |
| variant policy drop visibility | mirror `variantPoliciesDropped` | **covered** — `test_preflight_provider.cpp:364-387` |

## Conclusion

No live P0/P1 defect found in this track's ownership on master. Per the task's anti-fabrication clause ("如果某一轨道真实没有待修缺陷，不强行制造功能"), this track lands **no speculative code change**: its deliverables are the audit record above, fresh regression runs of the four modules' test families on the R16-B integration branch (protecting the cross-track broker API change), and the budget/caps inventory in `PERFORMANCE.md`.
