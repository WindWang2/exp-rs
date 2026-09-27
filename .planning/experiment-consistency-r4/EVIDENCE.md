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
| (待) | ③ 围栏自适应 + 分区两态 oracle | WP-B/WP-D |

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
- ⑦：待重链后补（stash evidence_source.cpp → overflow 目录被当 attempt-0 采纳）。

## 3. 变异效力（mutation potency）

- WP-E：删除 `saveMetricRecordsBatch` conflict 分支的 rollback → O-txn-1 红
  （directCount 见到部分行）；恢复 → 绿。42 断言/3 例。

## 4. 绿色证据（轻量道，每轮全量重跑）

- test_experiment_consistency_r4：218→243 断言 / 8→9 例（随 WP-D 增长），All tests passed。
- test_study_analysis：1374 断言 / 10 例，All tests passed（含 1001 边溢出端到端拒绝）。
- test_experiment_benchmark_r4：11 断言 / 3 例，All tests passed。
- test_experiment_parity_r4：126 断言 / 4 例，All tests passed。
- test_experiment_txn_r4：42 断言 / 3 例，All tests passed。
- test_experiment_capsule：261 断言 / 55 例，All tests passed。

## 5. 重链载体（build-r4 定向构建，待回填）

- ③ 围栏自适应 RED/GREEN。
- 基线 ctest `-R "experiment|capsule|debugger|study|scientific_state" -j1` 红绿分布。
- 终门禁双跑日志（两轮逐套件计数）。

## 6. 环境事实

- gcc-15（/usr/bin/g++-15，复刻 r3 成功构建栈；规避 raise-compiler-stack.sh 吞错陷阱，直接 cmake 无包装脚本）。
- Ninja；`-j2` 全程（nproc=16，机器另有 3 条并行轨道编译，load 20+ 仍守红线）。
- `QT_QPA_PLATFORM=offscreen`；`CTEST_PARALLEL_LEVEL=1`。
- 同目录双 ninja 曾死锁一次（构建策略切换时）→ 已改单目录单构建 + 独立轻量目录 build-r4-light 专供快循环；最终门禁将在此定向构建产物上执行（同一 build 目录语义，全新 configure 事实见 BASELINE §5）。
