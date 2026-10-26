# ZCode R21-D FINAL_REPORT

Track D — Qt/QGIS 工作台、地图交互、编图出图与 UI 一致性。
会话：2026-10-24（单会话多轮 goal-loop 迭代：M0 盘点 → 5 个修复波次 → R1/R2 双独立评审闭环 → PR）。

## ① 基线复核（LIVE）

- 执行时刻 `origin/master` = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（git fetch 与 GitHub API 双确认）。
- Open PR = 0；Open Issues = 126（全量枚举分诊）。
- 兄弟轨 worktree 在场：R21-A/B/C/E（共享构建锁串行）。
- 本轨 worktree：`exp-rs-r21-D-20231023T214750Z` → 实际 `/home/kevin/project/exp-rs-r21-D-20261023T214750Z`，分支 `agent/r21-d-qt-qgis-cartography-20261023T214750Z`，10 commits（911e0b17a3 → 01b624c401）。

## ② 126 open issues 分诊结论（源码实证）

D 轨白名单内 P1/P2 候选**全部 FIXED-OPEN**（master 已修，复测绿，未重复修）：
#1535（async_algorithm_runner feedback 所有权）、#1536（kLoadErrorEntryRows）、#1537（stac lastPageGeneration）、#1559（workspaceService seam 释放）、#1560（R20-04 GDAL 取消探针）、#1561（spectral 离线程采样）、#1562（log_panel refilter）、#1563（CSV RFC4180）、#1564（va probe startPending 节流）。
OUT-OF-SCOPE：#1565/#1557/#1558/#1539 等（teaching/native/处理域）。**唯一 live 遗留 = W1b**（R19-03→R20-04 移交：属性表编辑绕过 DataManager Edit Lease）→ 本轨修复。

## ③ 本轨交付（10 commits，每个 RED→GREEN×2）

| 波次 | 提交 | 内容 |
|---|---|---|
| M2-1 | b83f31386c | **W1b**：SicnuEditLeaseCoordinator seam；facade saveEdits/toggleEditing（逐层+批量）、QgsGuiVectorLayerTools start/save/stop 与 shell 同一 lease bracket；身份快照+重解析防协调换层 UAF；属性表 editingToggled 空指针守卫 |
| M2-2 | b29c751ea8 | **P1** atlas filter 解析失败静默丢弃→编译拒绝+验证 armed；**P2** exportMapLayout cancelProbe（页边界轮询+回滚+operator/produce 映射 Cancelled/PRODUCE_CANCELLED） |
| M2-3 | 851151b449 | **P2** frame-declared CRS 编译进 QgsLayoutItemMap（无效 CRS 拒绝） |
| M2-4 | 641dec9d4f | **P3** applyItemProperties 布尔键类型守卫+undo 配对；重复页索引 typed refusal；repair 进度单调 [0.30,0.55] |
| M2-5 | d3d356f9f2 | **P3** layout:export 交付集从事后目录重扫改为 exportLayout 出参+导出后有界清理 |
| M6 | db93b2655e | **R1 闭环**：P1-1 extent/CRS 单位契约变换（含 4326 回落）；13 处类型守卫补全；单文件取消 seam；清理移到导出成功后；OWNERSHIP 补 mapspec；produce 探针语义 |
| M6 | 8c3ff74bd6 | **R2 闭环**：produce 探针不再每轮询推事件（R1 虚假闭环纠正）；4 处遗漏 asString 守卫；grid interval 单位变换；mapspec-reference/USER_GUIDE 文档；QgsCsException 改 typed 拒绝；清理与出参解耦；协调器 showConflictWarning；facade commit 失败上报 |
| docs | 75a9bdf247/01b624c401 | REVIEW_R1/R2 记录 |

## ④ 证据等级

- **RED→GREEN×2**：每个修复波次均以 stash 生产修复（或删对象强制重编）取得真实 RED，再恢复 GREEN×2。关键 RED 证据：W1b 6 断言红（hasActiveEditLease=false/revision 未前进）；P1-1 extent 4 断言红（度值 116.09 > 1e7 false）；P3-1 编译期契约缺失；P2-2 链接期契约缺失。
- **新增测试**：10 个 TEST_CASE（CP11 4 + layout_tools 2 + r14_D 4：W1b 4 + R1-P3-6 分支 3 中合并计数）。
- **全绿族**：r14_D 440/24、raster_processing_dialog_base 120/11（R20 cancel seam）、layout_tools 1338/16（R20 DPI seam）、mapspec 2830/239（raw schema registry 腿）、qgis_display_manager 6490/25、command_registry 57/11、selection_context 72/15、full_shell_lifecycle 126/7、view_link 1308/16（PROJ_DATA 补齐）、stac_client 28/3、spectral_profile_widget 154/16、log_panel 7/4、accuracy_panel_wiring 8/2、guided_workflow_widget 124/11、async_algorithm_runner 8/2、layout_designer 48/9、editing_e2e 34/1、i18n 401/6、theme_selector_parity 102/2、task_center（#1454 竞用例 flaky，3 次 1 绿，src/processing 非本轨域）。
- **KNOWN_BASELINE_FAIL 诚实分类**：`test_cartography_production_11` 默认 3/32 红（:812/:1199/:1335），与 R19-03/R20-04 记录逐点一致；`SICNU_CP11_RENDER=0` 时 27 passed + 5 skipped、**0 failed** → 主机 legend-paint 渲染路径差异（门注释记载），非产品缺陷。未改 golden、未跳过门。
- **环境伪红归因**：test_view_link 3 断言（PROJ_DATA 缺失）；test_task_center 1 用例（load 下竞态 flaky）。

## ⑤ 独立评审闭环

- **R1**（fresh reviewer）：**REJECT**（1×P1 + 3×P2）。P1-1 是 frame-CRS 修复**新引入的静默错渲回归**（声明 crs 无 extent 时项目度数被按声明 CRS 单位解释）——评审员以探针实证。全部修复于 db93b2655e。
- **R2**（不同 fresh reviewer）：**APPROVE-WITH-FIXES**（4×P2）。独立 RED 重放证实 R1-P1-1/P2-2/P2-3 真实修复；发现 R1 的 P3-1 闭环为**虚假声明**（produce 探针照旧每轮询 report）+ 4 处遗漏类型守卫 + grid interval 单位分裂。全部修复于 8c3ff74bd6。
- 两轮验收矩阵见 REVIEW_R1.md / REVIEW_R2.md；P3 记录项均附理由与 owner（本轨）。

## ⑥ 交付

- PR：见终报底部（`gh pr create` 结果；未 auto-merge、未等线上 CI）。
- HEAD：`01b624c401`；BASE `911e0b17a3`。
- Worktree：`/home/kevin/project/exp-rs-r21-D-20261023T214750Z`。
- 账本/证据：`.planning/r21-D/{BASELINE,ISSUE_TRIAGE,OWNERSHIP,PR_COLLISION_MATRIX,RISK_REGISTER,TASKS,ORACLES,LOOP_LEDGER,RED_GREEN→TEST_RESULTS,PERF_BASELINE→N/A,REVIEW_R1,REVIEW_R2,TEST_RESULTS,CROSS_TRACK_FINDINGS,FINAL_REPORT,PR_BODY}.md`。

## ⑦ Oracle checklist

| Oracle | 状态 | 证据 |
|---|---|---|
| O-D0 lease 一致性 | ✅ | W1b 4+3 用例（440/440）；五分支走查（R2 独立） |
| O-D1 6 用户路径 | 🟡 | 既有 offscreen 测试族全绿（editing_e2e/layout_designer/task_center 等）；未新造路径测试（以缺陷修复+回归保护优先） |
| O-D2 状态转移/无 crash | ✅ | 邻域套件全绿；W1b 重解析测试 |
| O-D3 PDF/SVG/PNG/atlas DPI | ✅ | layout_tools 1311/14（R20 DPI 四 seam）；cancel seam 新增 |
| O-D4 主题/高 DPI/中文/坏数据 | 🟡 | theme_selector_parity 102/2、i18n 401/6 绿；未新造视觉记录 |
| O-D5 R20 三 seam + CP11 分类 | ✅ | 120/11 + 1311/14 + 2830/239；CP11 3 红如实分类 |
| O-G1 盘点证据 | ✅ | BASELINE/ISSUE_TRIAGE（126 条） |
| O-G2 RED/GREEN×2 | ✅ | 每波次 stash-RED 记录 |
| O-G3 性能基准 | 🟡 | 无新增热点（轮询 O(页数)、清理 O(16)）；PERF_BASELINE N/A（本轨无性能目标项） |
| O-G4 两独立评审 | ✅ | R1 REJECT→修；R2 APPROVE-WITH-FIXES→修 |
| O-G5 skip/环境红归因 | ✅ | TEST_RESULTS 逐项 |
| O-G6 白名单+无回滚 | ✅ | 29 文件全在 OWNERSHIP；R20-04/R18-D/R19-03 契约复跑绿（R2 独立验证） |
| O-G7 无假实现 | ✅ | 无 TODO/占位；RED 均为真实运行 |

## ⑧ 未完成项（如实）

- O-D1/O-D4 的"新造"用户路径/视觉记录未展开（本会话以审计驱动缺陷修复与双评审闭环为优先；既有 offscreen 覆盖已全绿）。
- 双窗口集成测试未构造（NOT_EXECUTED：QgisApp 单例协调器的第二窗口场景由 guarded release 覆盖代码级）。
- W1b 的 R2 记录项（恢复被覆盖上一版文件、>16 页 sibling 孤儿、跨表面 page_count 语义）均为已知限制，附理由记录于 REVIEW_R2.md。
