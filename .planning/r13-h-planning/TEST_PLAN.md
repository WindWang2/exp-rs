# R13-H Test Plan & Verification Strategy

## Test Verification Protocol
1. **RED Fixture**: For each defect or contract boundary, establish a minimal reproducible failing test case (or identify existing gaps).
2. **Local Fix**: Implement minimal, surgical fix strictly within the track's writable boundary.
3. **GREEN x 2**: Run tests twice consecutively to confirm zero flakiness.
4. **Adversarial / Stress / Boundary Testing**: Exercise NaN, Inf, negative values, boundary string lengths, null inputs, cancelled contexts, and scale limits.
5. **Commit**: Clean, semantic git commit with reference to work package / issue.

## Test Target Mapping

| Work Package | Primary Test Executable / File |
|---|---|
| WP-01 (ScientificGoal params) | `test_scientific_planner_schema` |
| WP-02 (PlannerCore determinism) | `test_scientific_planner_core`, `test_scientific_planner_drift` |
| WP-03 (Rule spine & gates) | `test_scientific_planner_constraints`, `test_scientific_planner_golden` |
| WP-04 (Plan IR projection) | `test_scientific_planner_ir` |
| WP-05 (OpenQuestions & facts) | `test_scientific_planner_core`, `test_scientific_planner_proposal` |
| WP-06 (Provider isolation) | `test_scientific_planner_core` |
| WP-07 (RepairPolicy whitelist) | `test_workflow_repair_rules` |
| WP-08 (RepairPlanner ranking) | `test_repair_planner_completion` |
| WP-09 (Repair schema & state) | `test_repair_planner_schema` |
| WP-10 (ScienceContext cache key) | `test_science_context_broker` |
| WP-11 (Capability/Recipe router) | `test_science_context_live_authorities` |
| WP-12 (Context budget) | `test_science_context_broker` |
| WP-13 (Observability & asset state) | `test_science_context_concurrency` |
| WP-14 (Suitability contract) | `test_suitability_adversarial` |
| WP-15 (Plan explanation loop) | `test_scientific_planner_review`, `test_scientific_planner_teaching` |
| WP-16 (Performance & scaling) | `test_scientific_planner_core` (benchmark section) |

## Test Execution Command Pattern
```bash
ctest --test-dir build-r13-h --output-on-failure -j1 -R '^test_scientific_planner_'
ctest --test-dir build-r13-h --output-on-failure -j1 -R '^test_repair_planner_'
ctest --test-dir build-r13-h --output-on-failure -j1 -R '^test_science_context_'
ctest --test-dir build-r13-h --output-on-failure -j1 -R '^test_suitability_'
```
