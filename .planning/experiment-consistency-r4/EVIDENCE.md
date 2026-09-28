# R4 EVIDENCE — 证据链归档

轨道：hardening/r4-experiment-consistency（Track 11）；基线 `origin/master = 15e5c66b5`（PR #1333 合并点，2026-09-27 实测与提示词一致）。

## 1. 提交清单（随进度回填）

| 提交 | 内容 | WP |
|---|---|---|
| 2ba843aa30 | ⑤ outgoingEdges edge-kind 过滤下推 SQL + runsForCell 直分页 | WP-B |
| a2684e0104 | ⑥ twin 扫描上限显式化（twinScanCapped/twin_scan_capped） | WP-B |
| 9ed48f0a4f | ④ runsForCell typed 溢出拒绝 + study 全链传播 | WP-B |
| 455fed2857 | ② benchmark 重复行 typed 拒绝 | WP-B |
| 218eb5fc94 | ① flattenMetrics 结构不对称 typed notes | WP-B |
| 874eb6c0a2 | ⑨⑪ typed 单记录读取器 + lineageEdgePage | WP-B |
| 1d05e18279 | ⑨ graph 实验侧截断标记穿透 | WP-B |
| 7f62b2b0c9 | ⑪ 四判定路径迁移（promotion/ref/debugger/matrix） | WP-B |
| 08c552f6a2 | ⑩ splits 跳过 qWarning 显式化 | WP-B |
| 4504c14dea | ⑧ legacy seed 无损域拒绝 | WP-B |
| e7781f3843 | ⑧⑨⑩⑪ 一致性套件（8 例 oracle） | WP-B |
| 045df87975 | WP-C studio↔store parity 四 oracle | WP-C |
| 33f65ef97c | WP-E 事务边界三 oracle + 变异效力 | WP-E |
| e77443ddbc | ⑫ capsule fixture 级重放闭环 | WP-F |
| cd85a3fea0 | WP-F study spine 边界两 oracle | WP-F |
| 27b963d40e | ③ fence oracle + ⑦ attempt-scan oracle + 分区两态 oracle | WP-B/WP-D |
| 1f7a1d88e3 | planning 六件套落盘（强制 add，目录 gitignored 为仓库默认） | 归档 |
| 5925e8aed9 | 评审修复批次：P0-1 bridge 套件落树、P0-2 compactJson 前置声明、P1-1 analyzeStudy typed 读取器、P2-1..4、P3-1/P3-6 | Phase 5 |
| 438a249654 | runsForCell 三既有测试载体迁移收尾（续作补完中断轮，含 test_study_runner.cpp:309） | ④ 传播 |
| da0b08a454 | REVIEW_LOG 逐条处置回填（P3-5 声明的账面修正实际补落） | Phase 5 |
| edea399120 | merge origin/master a726d17a6（+425 提交；账本冲突双保留；白名单 src 零上游重叠） | 集成 |
| 53658dba92 | 删除 origin/master 自带的 tests/CMakeLists.txt 孤立 `=======`（master 当前无法 configure 的根因） | 集成必需 |

## 2. RED 证据（stash/checkout 回退实现后运行，均为本地实证）

- ⑤：`runs.size() 0 == 3`（噪声边挤占 LIMIT 页，recorded runs 全部被挤掉）。
- ⑥：twinScanCapped=false + JSON 键缺失（cap 不可见）。
- ②：两条重复行用例失败（refusal 缺失，重加权发生）。
- ①：mismatchNamed/arrayNamed 两断言失败（路径静默消失）。
- ⑩：skipNamed 失败（跳过无任何记录）。
- ⑧：1e19/-5 两条腿导入成功（无损性从未检查）。
- ⑪：promotion gate 读作 missingEvidence（success）而非 typed 拒绝。
- ④：合同缺失（Result API 编译失败即红）+ ⑤ 轮 RED 输出证明静默页行为。
- ⑨：同上（新 API 编译失败即红）。
- ⑦：已补（续作会话）：反向应用 7f62b2b0c9 于 evidence_source.cpp → 重链 test_experiment_debugger → attempt-scan oracle 红（`steps.has_value()` 展开为 true，损坏 attempt 号冒充 attempt-0 被采纳；6 断言 1 失败）→ 恢复修复重链 → 绿（7 断言）。RED/GREEN 双向实证。

## 3. 变异效力（mutation potency）

- WP-E：删除 `saveMetricRecordsBatch` conflict 分支的 rollback → O-txn-1 红
  （directCount 见到部分行）；恢复 → 绿。42 断言/3 例。

## 4. 绿色证据

### 4.1 轻量道（前会话，15e5c66b5 基线上）

- test_experiment_consistency_r4：218→243 断言 / 8→9 例（随 WP-D 增长），All tests passed。
- test_study_analysis：1374 断言 / 10 例，All tests passed（含 1001 边溢出端到端拒绝）。
- test_experiment_benchmark_r4：11 断言 / 3 例，All tests passed。
- test_experiment_parity_r4：126 断言 / 4 例，All tests passed。
- test_experiment_txn_r4：42 断言 / 3 例，All tests passed。
- test_experiment_capsule：261 断言 / 55 例，All tests passed。

### 4.2 union 树快反馈（续作会话 2026-09-28，merge a726d17a6 后 build-r4-light 增量重建 exit 0，28/28 载体直跑）

| 载体 | 结果 |
|---|---|
| test_experiment_consistency_r4 | 271 断言 / 10 例，All tests passed |
| test_experiment_benchmark_r4 | 11 断言 / 3 例，All tests passed |
| test_experiment_parity_r4 | 1138 断言 / 5 例，All tests passed |
| test_experiment_txn_r4 | 42 断言 / 3 例，All tests passed |
| test_experiment_bridge_r4 | 16 断言 / 3 例，All tests passed（③ fence oracle + 分区两态在内） |
| test_study_analysis | 1374 断言 / 10 例，All tests passed |
| test_study_spec / sampling / runner / spatial / export / exemplars | 63/9、166/10、133/13、69/7、3931/9、168/4，全部 passed |
| test_experiment_debugger | 529 断言 / 61 例，All tests passed（含 ⑦ oracle） |
| test_experiment_capsule | 261 断言 / 55 例，All tests passed（含 ⑫ 重放闭环） |
| test_experiment_evaluation | 488 断言 / 31 例，All tests passed |
| test_study_e2e（真实 spine） | 168 断言 / 1 例，All tests passed |
| test_studio_live_e2e（真实 spine 闭环） | 115 断言 / 2 例，All tests passed |
| test_experiment_studio_dock | 55 断言 / 4 例，All tests passed |
| test_scientific_state_core/diff/fixtures/geo/provenance/resolver/review/teaching | 76/12、41/12、102/9、171/37、71/15、78/18、39/10、31/8，全部 passed |
| test_scientific_state_catalog | 42 断言 / 3 例，All tests passed（exit 0） |
| test_scientific_state_gdal | 三连跑 exit=0 全绿（76 断言/5 例）。备注：多套件顺序循环首跑曾现一次性 3 断言红（22 断言处中断），隔离复跑 ×3 不可复现；门禁双跑重点盯 |

树面 oracle 计数（TEST_CASE）：新文件 25（consistency 11 + benchmark 3 + parity 5 + txn 3 + bridge 3）+ 既有扩展 4（study_analysis 2、debugger 1、capsule 1）= **29 ≥ 15**（5 个新文件 + 3 个既有文件扩展，双门均过）；与门禁 ctest -N 输出对数见 §5。

- test_experiment_consistency_r4：218→243 断言 / 8→9 例（随 WP-D 增长），All tests passed。
- test_study_analysis：1374 断言 / 10 例，All tests passed（含 1001 边溢出端到端拒绝）。
- test_experiment_benchmark_r4：11 断言 / 3 例，All tests passed。
- test_experiment_parity_r4：126 断言 / 4 例，All tests passed。
- test_experiment_txn_r4：42 断言 / 3 例，All tests passed。
- test_experiment_capsule：261 断言 / 55 例，All tests passed。

## 5. 终门禁（2026-09-28，全新 build-r4-gate 目录，gcc-15/Debug/Ninja/无 PCH/ENABLE_TESTS=ON）

构建事实：全新目录 configure→构建 28+1 载体（含重链 qgis_core/qgis_gui 闭包）。两次 `-j2` 构建各遇一次 g++-15 ICE（段错误，每次不同 TU，崩溃栈均在诊断/GC 路径；同批 TU 在 light 道与第 4 次 `-j1` 构建中全部干净编过）→ 定性为高负载下（当时 3 ninja + 7 cc1plus 并发）PCH/内存压力型偶发，非代码错误；改 `-j1` 后 2451/2451 一次通过零 ICE。ICE 期间的漏配修正（BASELINE §5 配方补 `CMAKE_DISABLE_PRECOMPILE_HEADERS=ON`）已记账 3-9 轮。

### 5.1 ctest 门禁（`ctest -C Debug -R "experiment|capsule|debugger|study|scientific_state" -j1`，QT_QPA_PLATFORM=offscreen）

| 轮 | 结果 | 日志 |
|---|---|---|
| 1 | **100% tests passed out of 67**，0 Not Run | gate-logs/gate_run1.log |
| 2 | **100% tests passed out of 67**，0 Not Run | gate-logs/gate_run2.log |

### 5.2 oracle 可数性（本轨附加修复 e4261ece92：五个 r4 套件注册加 TEST_PREFIX，无前缀时 catch_discover_tests 以裸用例名注册，轨道正则退化为用例名过滤、oracle 不可数）

`ctest -R "_r4::"`：**24 个 oracle 条目逐一可数**（consistency_r4:: 10 + parity_r4:: 5 + benchmark_r4:: 3 + bridge_r4:: 3 + txn_r4:: 3），双跑 24/24 全绿（gate-logs/r4_oracle_run1/2.log）。

### 5.3 全二进制整卷双扫（29 载体完整套件，覆盖正则够不着的用例）

| 轮 | 结果 | 日志 |
|---|---|---|
| 1 | **29/29 PASS，FULL_SWEEP_OK** | gate-logs/gate_sweep1.log |
| 2 | **29/29 PASS，FULL_SWEEP_OK** | gate-logs/gate_sweep2.log |

要点：study_e2e（真实 spine NDVI 端到端）、studio_live_e2e（fake+real 双后端闭环）、study_export（3931 断言）双扫均绿；test_scientific_state_gdal 双扫绿（前次一次性红不再复现，见 §4.2 备注）。

### 5.4 完成门禁对照

| 门禁 | 要求 | 实测 |
|---|---|---|
| ctest 双跑全绿零新增失败 | 2× | 67/67 ×2 + 全卷 29/29 ×2 + oracle 24/24 ×2 |
| 12 条处置 | 12/12 | R4_BACKLOG.md 12 行全 FIXED/KEPT 带提交号 |
| oracle | ≥15 可数 | **24**（`_r4::` 前缀 ctest 可数）+ 既有载体扩展 |
| 原子提交 | ≥16 | **30**（`git rev-list --count origin/master..HEAD`） |
| 触碰文件 | ≥14 白名单内 | **41**，全部白名单内（delta 评审逐文件核过） |
| bundle 双跑 | ≥1 组 | §4.1/§4.2 双跑逐字节稳定 oracle（consistency_r4 内） |
| 规划工件 | 6 件齐 | BASELINE/PLAN/DECISIONS/EVIDENCE/REVIEW_LOG/R4_BACKLOG 齐 |

- ③ 围栏自适应 RED/GREEN。
- 基线 ctest `-R "experiment|capsule|debugger|study|scientific_state" -j1` 红绿分布。
- 终门禁双跑日志（两轮逐套件计数）。

## 6. 环境事实

- gcc-15（/usr/bin/g++-15，复刻 r3 成功构建栈；规避 raise-compiler-stack.sh 吞错陷阱，直接 cmake 无包装脚本）。
- Ninja；`-j2` 全程（nproc=16，机器另有 3 条并行轨道编译，load 20+ 仍守红线）。
- `QT_QPA_PLATFORM=offscreen`；`CTEST_PARALLEL_LEVEL=1`。
- 同目录双 ninja 曾死锁一次（构建策略切换时）→ 已改单目录单构建 + 独立轻量目录 build-r4-light 专供快循环；最终门禁将在此定向构建产物上执行（同一 build 目录语义，全新 configure 事实见 BASELINE §5）。
