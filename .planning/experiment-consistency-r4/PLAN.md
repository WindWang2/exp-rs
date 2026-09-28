# R4 PLAN — 工作包执行序（TDD 纵向切片）

节拍：每条/每 oracle 一弹 —— 红（复现现状缺陷的测试）→ 绿（最小实现）→ 原子提交 → 账本。
每个提交独立可编译；`ctest -R` 目标级验证，里程碑处全量。

## WP-A（Phase 1）backlog 定版
R4_BACKLOG.md 12 条 × {独立现状证据, 处置预案, 优先级}。已随 Phase 0 产出，处置中滚动更新。

## WP-B（Phase 1-2）12 条逐条处置
顺序按依赖与风险排序：
1. ⑤ outgoingEdges 过滤/限页次序（独立 SQL 修正，低风险起步）
2. ⑦ candidatePaths toInt ok（独立小修）
3. ⑥ 50-twin 上限显式标记
4. ④ runsForCell typed 拒绝/标记
5. ② benchmark 重复行拒绝（契约决策 → DECISIONS.md）
6. ① flattenMetrics 不对称显式化
7. ③ markdown ≥4 反引号边界
8. ⑧ seed 十进制安全范围诊断
9. ⑨ lineage 截断 API 化（allLineageEdges 返回结构 + ancestors/descendants 语义核对）
10. ⑪ runById/promotionById fail-closed（caller 盘点后定签名策略）
11. ⑩ splitManifestsForVersion 跳过显式化（白名单扩展点）
12. ⑫ capsule fixture 重放

## WP-C（Phase 2）studio↔store 真相 oracle（目标 1-6）
新测试 tests/test_experiment_parity_r4.cpp：同一 run 集合，studio 可观测状态集合 = store 落盘状态集合；批量 upsert 回滚合同、状态机 bad_transition、cursor 分页、runCount fail-closed、终态不可变、studio 投影。

## WP-D/WP-E（Phase 3）oracle 7-12 + 事务边界
- bundle 跨平台 digest 双跑（路径分隔符/EOL/locale 归一）
- 证据分区完备性（缺分区 typed 拒绝；空分区合法）
- store 多表事务边界（run+metrics+lineage 同成败，中途失败注入 → 前表无可见变更；对账直查落盘 SQLite）
- 批回滚"首败"行可数
- lineage 游标完备性
- repeat_execution twin 上限标记语义

## WP-F/WP-G（Phase 4）study spine 边界 + 防回归
- study 三边界：空 study / 超界 step id / 并发提交
- capsule fixture 重放链路
- tests/CMakeLists.txt 注册新目标；`ctest -R "experiment|capsule|debugger|study|scientific_state" -j1` 全绿双跑

## 里程碑门禁
- M1（Phase 2 末）：①-⑫ 全处置 + oracle 1-6 绿
- M2（Phase 3 末）：oracle 7-12 绿 + 事务注入证据
- M3（Phase 4 末）：oracle ≥15、全绿双跑、rev-list ≥16
- M4：独立评审 P0/P1 清零、PR 开出

## Oracle 记录表（随提交回填）

| # | oracle 名 | 不变式陈述 | 测试文件 | 状态 |
|---|---|---|---|---|
