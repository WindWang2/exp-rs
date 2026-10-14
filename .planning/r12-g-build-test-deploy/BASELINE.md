# R12-G BASELINE — 构建/测试验证链与离线交付工程收敛

- **方向**: R12-G（构建 `/` 本地验证 `/` 离线交付工程）
- **分支**: `hardening/r12-g-build-test-offline`
- **Worktree**: `C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test`（独立，不指向主 checkout）
- **派生基线**: `origin/master` = `28abef358217520dfbb802efdfdb11a2bf850465`（"Merge pull request #1482 from WindWang2/hardening/r11-d-qt-teaching-ux", 2026-10-13 22:40:27 +0800）
- **核查时间**: 2026-10-14 (GMT+8)
- **本地 master checkout**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`（落后 origin/master，未改动）

派生校验：`git worktree add -b hardening/r12-g-build-test-offline ../exp-rs-r12-g-build-test 28abef358`
`git merge-base HEAD 28abef358 == 28abef358` ✅（HEAD 直接等于基线，非落后派生）

## 1. 执行环境诚实声明（重要）

本轮宿主是 **WorkBuddy**，不是 Google Antigravity。环境中**不存在** `/teamwork-preview` 与 `/goal-loop` 两个斜杠命令，也不存在这两个 skill 的本地 SKILL.md。按指令"不能假装这些命令已经执行；如果宿主命令名称不同，读取其本地帮助/skills 配置再选兼容入口，不杜撰 API"，采用的等价机制是：

| Antigravity 机制 | 本轮实际使用的等价机制 |
|---|---|
| `/teamwork-preview` 多 agent 协作 | 并行 spawned subagent（3 个只读审计 agent）+ 主 Agent 单写入者统筹；共享任务列表与单写入者锁 |
| `/goal-loop` skill | 显式 Oracle 驱动循环：RED reproducer → 最小修复 → GREEN×2 → 独立 review → 账本（`.planning/r12-g-build-test-deploy/ORACLE_LEDGER.md`） |
| 独立 reviewer subagent | 实现完成后另起未参与实现的 reviewer subagent，两轮 |

**未执行**：`/teamwork-preview`、`/goal-loop` 两个命令本体。此项如实记为 `NOT_RUN`，不以"采用了团队模式"含糊带过。

## 2. 三层交叉核查结果

### 2.1 分支 / worktree

```
worktree C:/Users/wangj.KEVIN/projects/exp-rs                    master (8a086427)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r10-algorithms     hardening/r10-algorithm-processing-quality (8616585e)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r10-ui-teaching    hardening/r10-ui-teaching-help-consistency (8a7e0778)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r9-agent-runtime   hardening/r9-agent-execution-reliability (0e65fd5b)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r9-science-data    hardening/r9-science-data-integrity (2dfc6642)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r9-ui-teaching     hardening/r9-ui-teaching-test-quality (7bbe35b5)
worktree C:/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test   hardening/r12-g-build-test-offline (28abef358)  ← 本方向
```

远程分支：`master`、`hardening/r12-a-science-numerics`、`hardening/r12-c-agent-workflow-runtime`。
`git fetch --prune` 已清理 4 个已合并的 r9/r10 远程分支。**R12-A/B/D/E/F 在本机没有 worktree**（A、C 仅以远程分支存在）。

### 2.2 开放 PR（只有 2 个，与指令快照一致）

| PR | 标题 | head | 变更文件数 | inline 评论 |
|---|---|---|---|---|
| #1483 | hardening(r12-a): remote sensing scientific algorithms & numerical contracts hardening | `hardening/r12-a-science-numerics` (8fe92f959) | 42 | 0 |
| #1484 | hardening(r12-c): harden ScratchRegistry lifecycle, AssetStateProvider thread-safety, and Pi test suites (#1449) | `hardening/r12-c-agent-workflow-runtime` (a50c328fd) | 10 | 0 |

两个 PR 均无 inline review comment，`resolution=UNKNOWN`（REST 无法给出 GraphQL reviewThread 的 resolved 状态，不做推断）。

**#1483 变更文件（生产侧）**: `src/analysis/classification/{rs_classification_pipeline,rs_training_data_extraction}.cpp`、`src/analysis/segmentation/rs_segment_features.cpp`、`src/core/temporal_cube.cpp`、`src/operators/rs/*`（8 个）、`src/processing/algorithms/{math_utils,resampler,spectral_anomaly,nodata_utils}.{cpp,h}` + 12 个 `tests/test_*.cpp` + **`tests/CMakeLists.txt`**。

**#1484 变更文件**: `pi/knowledge/capability-temporal.md`、`pi/package.json`、`pi/test/scientific_workflow_compiler_11.test.mjs`、`src/runtime/chunk/scratch_registry.{cpp,h}`、`src/science_context/asset_state_provider.{cpp,h}`、`tests/test_external_memory_10.cpp`、`tests/test_science_context_broker.cpp` + **`tests/CMakeLists.txt`**。

### 2.3 开放 issues（20 项，#1447–#1486）

```
1486 python  worker pool handleWorkerLoss 无锁改写 / 数据竞争
1485 geospatial SSRF 内网判定字符串可绕过
1468 test(ci)  ← R12-G 主线索（见 §3）
1467 core-fork  scene-read 静默失败→NaN / radiometric preflight
1466 teaching   grader 无 Windows python 回退 / 发现启发式丢文件 / 非原子写入
1465 help       broker cache key 丢 typed failure cause / std::equal 越界
1464 verify     recordedAtUtc 原始字典序比较 / 可选 scenario 参数被忽略
1463 agent_ops  最旧证据淘汰丢 goal-normalization / bench 前缀比较 / recipe 重扫抛异常
1462 python     startWorker 失败留僵尸 node / 未知 method 无回复 / 重复算子注册
1461 analysis   first-band-only NoData / >255 class id 静默截断 / transform 失败回退
1460 cli,sdk    validateArgv 当成硬门禁 / PATHEXT 忽略 / batch runner 把失败记成 ok
1459 workflow   可选解引用 / 按错误消息子串的 fallback / coordinator mutex 下 IO
1458 experiment 可移植复现包未实现却标记 portable / studio 版本门被 && 削弱
1457 data       bounds-only 样本丢 sourceAssets / removeSample 非事务 / stepRow 吞 sqlite 错
1456 geospatial IPv4-mapped IPv6 未过滤 / 重定向绕过首跳 / Windows 非 ASCII ledger
1455 operators  corpse-session 误删替换会话 VRAM ledger / CancelToken 未接线 / OTB 看门狗 1h
1454 processing cancel-vs-submit 竞态 / committer 违反 temp 保留契约 / logBuffer 深拷贝
1453 app        watchTask 订阅竞态 / rs_scan_pool owner map 不清理
1450 authority  四份 SHA-256 分叉 / bounded reader 未用于 checkpoint / canonicalizer 绕过
1449 concurrency sqlite close() 无 store mutex / registry 返回悬垂指针
1448 stores     scan 错误当 exhausted / checkpoint 恢复无内容校验
1447 json       jsoncpp 无类型访问让 Json::LogicError 逃出类型边界（9 处，2 处启动崩溃）
```

**R12-G 认领**: #1468（主）、#1449（仅作并发门槛，不改 `src/`）、#1447（仅测试框架防崩溃面）、#1450（仅生成物/摘要一致性门槛）。

## 3. Issue #1468 逐条现场重核（不照抄 issue，以代码为准）

| # | issue 断言 | 现场核实 | 判定 |
|---|---|---|---|
| P1 | `tests/sicnu_test_main.cpp:54` `_exit(result)` 跳过全部静态析构 | 当前为 `return result;`（:64），注释显式记录 #1468 修复 | **ALREADY_FIXED**（仅此一个 TU） |
| P1-ext | 同一缺陷类在其他自带 `main()` 的测试 TU | **20 个 `tests/*.cpp` 仍为 `_exit(result)`**（详见 `REPRODUCERS.md`） | **STILL_BROKEN**（跨方向，见 OWNERSHIP.md） |
| P2 | `tests/support/mini_cog_server.h:112` recv 无 SO_RCVTIMEO，stop() join 挂死 | 待审计 A 结论 | 见 `AUDIT_G02_G03_G11.md` |
| P2 | `tests/CMakeLists.txt:423/:558/:596` 无保护 `find_library(NETCDF_LIBRARY ...)` | :427 已有 `find_library` + :428 `if(NOT NETCDF_LIBRARY)` 告警；但 :436/:437/:570/:607 仍直接 `target_link_libraries(... ${NETCDF_LIBRARY})` | **PARTIALLY_FIXED**（有告警，仍会把 NOTFOUND 字面量喂给 linker） |
| P2 | `tests/support/http_range_server.cpp:646` respond() 忽略 `close` 参数、Truncated 计费错 | 待审计 A 结论 | 见 `AUDIT_G02_G03_G11.md` |
| P2 | `tests/CMakeLists.txt:642` WIN32 块重复 link `test_io_fabric_catalog` | 待审计 B 结论 | 见 `AUDIT_G04_G05_G12.md` |

## 4. 本机构建环境（实测）

| 项 | 值 | 状态 |
|---|---|---|
| VS 2022 MSVC | `14.38.33130` (Hostx64/x64) | ✅ |
| CMake | `C:\Qt\Tools\CMake_64\bin\cmake.exe` (3.29) | ✅ |
| Ninja | `C:\Qt\Tools\Ninja\ninja.exe` | ✅ |
| Qt6 | `C:\deps\Qt\6.8.0\msvc2022_64` | ✅ |
| QCA / Keychain | `C:\deps\qca-install` / `C:\deps\kc-install` | ✅ |
| vcpkg | `C:/deps/vcpkg` + 复用已安装树 `exp-rs/build-dev/vcpkg_installed` (x64-windows) | ✅（须 `VCPKG_MANIFEST_MODE=OFF`，否则联网引导失败） |
| Catch2 | `C:/deps/catch2-src`（`FETCHCONTENT_SOURCE_DIR_CATCH2`，离线） | ✅ |
| shell | `cmd.exe` 被宿主安全策略全面拦截 → 无法 `call vcvars64.bat` | ⚠️ |
| 变通 | PowerShell `Import-Module Microsoft.VisualStudio.DevShell.dll` + `Enter-VsDevShell -arch=x64` | ✅ |

**资源纪律**: `CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`QT_QPA_PLATFORM=offscreen`。

## 5. 已执行的基线 Oracle（真实输出）

| Oracle | 命令 | 结果 |
|---|---|---|
| build.sh hermetic selftest | `bash scripts/build.sh selftest` | **ALL PASS**（15 项：jobs cap 拒绝 exit 2、Unicode/空格路径引用、ctest -j1、offscreen） |
| 预设卫生 | `python scripts/check_presets.py CMakePresets.json` | **ALL PASS**（30 项：6 个 configure/build preset、4 个 test preset 的 jobs/ctest/offscreen/verbatim 断言） |
| resource_guard 单元 | `python -m pytest scripts/dev/tests/test_resource_guard.py` | **NOT_RUN**（本机无 pytest 模块；已在账本记录，改用 unittest 直跑） |
| 真实 configure（离线） | cmake `--preset dev-default` + `VCPKG_MANIFEST_MODE=OFF` | 见 `PERFORMANCE.md` / `ORACLE_LEDGER.md` |

## 6. 冲突矩阵（R12-G vs 活跃写入者）

| 路径 | #1483 | #1484 | R12-G | 处置 |
|---|---|---|---|---|
| `tests/CMakeLists.txt` | ✏️ | ✏️ | 需要 | **只读短租约**；本方向如需改注册，先记录单写入者申请 + rebase 后重做，不抢写 |
| `src/**` 全部 | ✏️(A) | ✏️(C) | — | 完全不碰 |
| `tests/test_*.cpp`（A/C 改名清单） | ✏️ | ✏️ | — | 不碰 |
| `tests/support/**`、`tests/sicnu_test_main.cpp` | — | — | ✅ owner | 可写 |
| `scripts/**`、`cmake/**`、`packaging/**`、`CMakePresets.json` | — | — | ✅ owner | 可写 |

**跨方向 `_exit` 缺陷（20 个 TU）处置**: 见 `OWNERSHIP.md` — 不修改其他方向的测试 TU，改为在 R12-G 自有的 `tests/support/**` 层消除实际危害。
