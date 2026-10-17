# R16-B-T03 Test Matrix

All runs: `ctest` (cmake-4.1.2), `-j1`, `QT_QPA_PLATFORM=offscreen`, `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib:/usr/lib`, build preset `dev-default` (Debug), `-j2` builds, in the track worktree.

| Suite | Scope | Pre-fix | Post-fix run 1 | Post-fix run 2 (integration) |
| --- | --- | --- | --- | --- |
| `test_science_context_broker` (40 cases) | broker contracts incl. new lifetime oracle #880 | **39/40** (#880 FAILED — RED evidence) | **40/40** | see REVIEW/TEST_RESULTS in integration |
| `test_science_context_concurrency` (14 cases) | cache/provider/observability/lifetime + 2 new churn regressions | n/a (tests added post-fix; deterministic RED carried by #880) | **14/14** | see integration |
| `test_science_context_goal_projection` | planner projection seam | unchanged code | run recorded below | see integration |
| `test_science_context_live_authorities` | broker + real registry + GDAL collector | unchanged code | run recorded below | see integration |
| `test_preflight_authority_invalidation` | cross-module consumer of `setCapabilityFacts` | compile+run on integration (no `capabilityFacts()` consumer — verified by grep) | — | integration |

## New oracle definitions

1. **`capability facts snapshot stays answerable across an authority reinstall`** (`[science_context][r16b][lifetime]`): install authority A (1 entry) → take snapshot → install authority B (2 entries) → the snapshot must still report A's authority string, A's entries and A's revision; the broker must serve B afterwards. Deterministic; failed 3 assertions pre-fix.
2. **`TC_R16B_01_CapabilityToolSurvivesAuthorityReinstallChurn`**: 2 threads × 300 tool calls against a broker whose authority is reinstalled in a tight loop; every call must return without exception and be internally coherent (authority+count matching one quiescently-calibrated authority). Calibrated predicate — no dependence on router internals.
3. **`TC_R16B_02_RecipeSearchToolSurvivesRouterReplacementChurn`**: 2 threads × 300 `recipeSearch` calls against alternating router contents (a0..a7 / b0..b7); every call must return exactly 8 hits sharing one snapshot prefix.
