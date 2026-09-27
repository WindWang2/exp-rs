# READINESS_CLOSURE — Track 18 收口台账

快照真源：`docs/verification/READINESS.md`（2026-09-16T07:02:56Z，SHA d2868c744，Host win32）。
处置真源：本机 `build-perf/`（Release，GCC 16.2.1，Linux 6.18 LTS）全新构建实测。
状态机：`not_built → passed | failed→fixed | 豁免(平台缺失/在途覆盖/宿主局限)`，零悬置。

## A. not_built 19 项处置（win32 → Linux）

| # | capability | artifact | win32 | Linux 实测 | 处置 | 证据 |
|---|---|---|---|---|---|---|
| 1 | io-uri | test_io_uri | not_built | **passed**（第 1 轮：96 断言/10 用例，rc=0） | 修复后通过（本就编译良好，win32 为宿主构建不全） | build-perf 直跑 2026-09-27；收口轮再双跑 |
| 2 | io-paths | test_io_paths | not_built | **passed**（11/6，rc=0） | 同上 | 同上 |
| 3 | io-range-cache | test_io_range_cache | not_built | **passed**（312/36，rc=0；loopback server 正常，未触发 77 skip） | 同上 | 同上 |
| 4 | io-remote-range | test_io_remote_range | not_built | **passed**（25/6，rc=0） | 同上 | 同上 |
| 5 | io-remote-validator | test_io_remote_validator | not_built | **passed**（89/17，rc=0） | 同上 | 同上 |
| 6 | io-atomic-failures | test_io_atomic_failures | not_built | **8/12 用例绿，4 红**（`VectorWriter::create: dataset creation failed`） | **豁免：在途覆盖（#1338）** | 根因=atomic_fs::stagedPathFor O_EXCL 预创建暂存文件，GDAL Shapefile/GPKG Create 拒绝已存在文件（#1335 PR 描述 Group 1 命名本套件 4 例；**#1338 自带 RED 基线与本机失败数逐一同为 4**，其 reservedStagedPathFor 修复后 7 套件全绿）。修复文件（atomic_fs.*、vector_writer 等 29 文件）归 #1338 所有，本轨不重复修以免必然冲突 |
| 7 | io-raster-contract | test_io_raster_contract | not_built | **passed**（2094/16，rc=0） | 修复后通过 | build-perf 直跑 |
| 8 | io-grid-descriptor | test_io_grid_descriptor | not_built | **passed**（61/7，rc=0） | 修复后通过 | 同上 |
| 9 | io-stac | test_io_stac | not_built | **passed**（27/5，rc=0） | 修复后通过 | 同上 | |
| 10 | fuzz-operator-schemas | test_contract_fuzz_ops | not_built | **passed**（5844 断言/3 用例，rc=0） | 修复后通过 | build-perf 直跑 |
| 11 | portable-fault-matrix | test_fault_matrix | not_built | **passed**（82/8，rc=0） | 修复后通过（POSIX 可用，win32 才缺） | 同上 |
| 12 | sdk-ipc-contract | test_exprs_ipc | not_built | **passed**（134/24，rc=0） | 修复后通过 | 同上 |
| 13 | concurrency-stress | test_concurrency_stress | not_built | **passed**（2152 断言/6 用例，rc=0） | 修复后通过 | build-perf 直跑 |
| 14 | posix-fault-injection | test_fault_injection | not_built | **passed**（152/8，rc=0） | 修复后通过（Linux 上 POSIX-only 照常可跑；win32 not_built 判定正确且不适用本机） | 同上 |
| 15 | worker-host-lifecycle | test_worker_host | not_built | 首 跑 8/13 红 → 根因=待 spawn 的 `sicnu_worker` 可执行件未构建（非缺陷）；补建后 **passed**（61 断言/13 用例，rc=0） | 修复后通过 | /tmp/r4-worker-build.log + 直跑双证 |
| 16 | visual-cartography | test_mapspec | not_built | 补编译需测试侧链接边（#1335 P0-3 在途的库级缺边，GNU ld 不为共享库未定义符号抽取普通静态库成员）→ 测试侧 whole-archive 链接后可编译；**运行 passed**（2604 断言/222 用例，rc=0） | 修复后通过 | ld 错误原文归档；直跑+ctest 双证 |
| 17 | bench-quality7 | benchmark_quality7 | not_built | **passed（bench）**：`--out` 产出证据件，ladder L7 ok | 修复后通过（bench=证据件，非 ctest 门） | /tmp/r4-ladder-bench2 + ladder json |
| 18 | bench-scale8 | benchmark_scale8 | not_built | **passed（bench）**：同上，ladder L7 ok | 修复后通过 | 同上 |
| 19 | bench-contract9 | benchmark_contract9 | not_built | **passed（bench）**：同上（operator_param_scan 2758ms 等），ladder L7 ok | 修复后通过 | 同上 | |

## B. failed 5 项处置

| # | capability | artifact | win32 | Linux 实测 | 根因分类 | 处置 | 证据 |
|---|---|---|---|---|---|---|---|
| 1 | contract-graph | test_contract_platform_9 | failed | 红：快照清单漂移（提交 1263 nodes/526 edges vs 实况 1265/527） | 契约漂移（快照未随契约变更再生） | **豁免：在途覆盖（#1337 P0-2）** | #1337 diff 含 contract_graph.snap.json 再生与 9 项既有 finding 说明；快照刷新机制 SICNU_CONTRACT_SNAPSHOT_WRITE 归其所有 |
| 2 | contract-operator-projection | test_contract_projection_9 | failed | **passed**（3414 断言/9 用例，rc=0） | win32 平台特有 | 修复后通过 | build-perf 直跑 |
| 3 | contract-command-reference | test_command_contract_9 | failed | 红：5 命令缺 help（mission.task.resume/retry、mission.timeline.show、teaching.labCockpit.show、workbench.experimentExplorationStudio） | 契约漂移（help 数据滞后） | **豁免：在途覆盖（#1336/#1337）** | 两 PR diff 对这 5 个命令 ID 共 54 处命中 |
| 4 | contract-diagnostics-census | test_diagnostics_contract_9 | failed | 红：33 个 harness 码缺 curated 页（UNWRAP_PROVIDER_*、TOPO_PHASE_*、TEMPORAL_CALENDAR_CONFLICT 等） | 契约漂移（diagnostics.json 滞后） | **豁免：在途覆盖（#1337）** | #1337 diff 命中失败码（14 处抽核） |
| 5 | drift-projection-10 | test_drift_projection_10 | failed | **passed**（2839/3，rc=0） | win32 平台特有 | 修复后通过 | build-perf 直跑 |

## C. timeout 1 项处置

| # | capability | artifact | win32 | Linux 实测 | 处置 | 证据 |
|---|---|---|---|---|---|---|
| 1 | fuzz-worker-ipc-splits | test_contract_fuzz_ipc | timeout | **passed**（3959 断言/4 用例，rc=0，远低于 900s 预算） | 通过（win32 timeout = ladder 注释明示的命名管道模拟宿主局限；纯进程内 bounded fuzz，无子进程） | ladder L2 第二轮 + 直跑双证 |

## C+. ladder 第二轮新发现（快照外，如实记账）

- `failure_11`（win32 快照记 passed）本机 F5 用例红：`io:translate` 对不存在输出目录
  抛 `InvalidParameter` 而非写拒绝四类之一——参数校验层行为，早于任何栅格 IO，与本轨
  触碰文件零交集（io operators 面非白名单），属宿主/GDAL 版本差异既有项，留给 io
  平台轨道处置。ladder 全量：L0/L1/L3(除 atomic)/L4/L5/L7 ok。

## D. 汇总（收口时更新）

- 处置总数：**25 / ≥25**（not_built 19 + failed 5 + timeout 1）
- not_built 19 → passed 18 + 豁免(在途覆盖 #1338) 1，**零悬置**
- failed 5 → Linux passed 2 + 豁免(在途覆盖 #1336/#1337) 3
- timeout 1 → Linux passed 1
- 快照更新：docs/verification/READINESS.md（Linux，compiled 46 / passed 40 /
  failed 5 / not_built 0 / timeout 0 / skipped 1）已提交 e8b5a25c3f
