# R25-A TEST_RESULTS（实测）

## 构建
```
cmake -S . -B build-r25-A -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON   # exit 0
flock $HOME/.cache/exp-rs-build.lock cmake --build build-r25-A --parallel 2 --target <8 r25_a 二进制>  # exit 0
```
（首轮全目标构建遇 reconfigure 时序问题——注册块追加在 configure 之后；重新 cmake 后 8 二进制全部 Built，exit 0）

## GREEN×2（HEAD，直跑二进制，exit 0）
| 测试 | run1 | run2 |
|---|---|---|
| test_r25_a_terrain_solar_latitude | 32 assertions / 5 cases passed | 同 |
| test_r25_a_terrain_flow_precision | 123 / 4 passed | 同 |
| test_r25_a_spectral_guards | 105 / 6 passed | 同 |
| test_r25_a_sar_geometry_fallback | 56 / 2 passed | 同 |
| test_r25_a_qa_confidence | 44 / 2 passed | 同 |
| test_r25_a_change_area_table | 25 / 2 passed | 同 |
| test_r25_a_optical_chain_e2e | 44 / 1 passed | 同 |
合计 **429 assertions ×2 全绿**（RED 证据见 ORACLES.md 的判别说明：master 行为在注释中标出，如 daylightSamples=9 vs 13、float32 累积 16785408 vs 精确 16785409、SID=0.0 vs inf、导数 1000×、incidenceDeg=30 vs 40 等）

## R2 闭环后重验（HEAD = 27a9eab316..ade8f377e1）
- 7 个测试二进制 schema 变更后全部重建并重跑：429 assertions 全绿（同上表）
- **test_surface_snapshot：All tests passed (17868 assertions, 7 cases)** —— R2-2 重新生成快照后门禁绿
- surface_diff_snapshot.json 由 surface_snapshot_gen 重新生成（16+/6-，含 cloud_confidence）

## master-RED 内核实测（独立编译，git show origin/master）
```
MASTER-SID disjoint = 0.000000            (HEAD: +inf)
MASTER-SID classify label=0 div=0.000000  (HEAD: label=1, finite div)
MASTER-ACC corner = 16785408 exact = 16785409 delta = 1  (HEAD: exact)
```

## 邻域回归（既有测试，HEAD 无新增失败）
| 测试 | 结果 |
|---|---|
| test_terrain_solar | All tests passed (80 assertions, 6 cases) |
| test_terrain_hydrology | All tests passed (1023, 12) |
| test_terrain_foundation5 | All tests passed (277, 14) |
| test_terrain_viewshed | All tests passed (1457, 8) |
| test_terrain_analytics_e2e | All tests passed (1088, 9) |
邻域合计 3925 assertions 全绿。

## 性能基线
见 PERF_BASELINE.md（3 套，5 次中位 + VmHWM；test_r25_a_perf_baseline 40 assertions 全过）

## 合并完整性
`git diff --check` exit 0；`check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`：1071 = 1063 + 8，lost 0+0，0 标记，0 重复 → PASS
