# ZCode R20-04 FINAL_REPORT

Track 04 — Qt/QGIS 工作台、交互状态、制图 MapSpec 与 UI 体验质量。
会话：2026-10-23 02:50–09:00 CST（单会话 7 轮 goal-loop 迭代）。

## ① 基线复核（LIVE）

- origin/master = `28e1891390bb812dc7ef2fe045ac90492c9525f1`（fetch 后；PR #1663–#1667 全 MERGED，无 open PR；126 open issues 多页遍历 → BASELINE.md）。
- R20 兄弟轨（01/02/03）worktree 文件域核对：与本轨写区零交集。
- 分支：`agent/r20-04-qt-qgis-cartography-20261022T190613Z`（6 commits），worktree `exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`。

## ② 本轨修改与闭环 Issue

| 项 | Issue/来源 | 提交 | 状态 |
|---|---|---|---|
| GDAL 长任务及时取消（探针 + GDALProgressFunc 适配器） | #1560 [P2]（live 复核仍 OPEN，master 现码确认未修） | f9bbe9a5f6 + edc7c27e17 + 8d41eecd3c | **本轨修复闭环**（seam 契约 + 确定性测试；生产对话框走 runOperatorTask，范围诚实已在 PR 声明） |
| 7 个 cartography 工具 raw schema 规范化 | R19-02 CT-6（跨轨遗留，归属本轨） | 7e147f0361 | **闭环**（registry 腿红→绿；快照/wire 不变） |
| 导出 DPI 下界统一 [72,1200] | R19-03 跨轨 #4 | b7a04d7465 + edc7c27e17 | **闭环**（layout seam + GUI 对话框 min 36→72；四 seam 一致） |
| W1b 属性表绕过 Edit Lease | R19-03 W1b | 只读对照（CROSS_TRACK #1） | 跨轨移交（需 data 域联合） |
| #1632 [P3] src/app 技术债 | live triage | 条目 1/2/5 上游已修（证据）；3 不改（测试使用中，"无使用者"过期）；4 移交 | ISSUE_TRIAGE.md |

## ③ 证据等级

- **RED（master 实跑，三 oracle 全部确定性红）**：
  - O-1 `300 (0x12c) < 300 (0x12c)`（test_raster_processing_dialog_base.cpp:160，种子提交本工作树实跑）；
  - O-3 `!true` / `false`（test_layout_tools.cpp:774/:775）；
  - O-2 registry 腿 @ cartography:chart_list（:322，本工作树 + R20-01 master 二进制双取证；全套 6/7 过 → 缺项仅在 raw）。
- **GREEN ×2（HEAD）**：9 个二进制两轮全绿 —— 120+120 / 17868+17868 / 5506+5506（含 CLI 腿）/ 1311+1311 / 29+29 / 132+132 / 2830+2830 / 9956+9956 / 169+169；全部 exit 0。
- **KNOWN_BASELINE_FAIL**：test_cartography_production_11 3/28 ×2 轮逐位一致（:811/:1160/:1296），与 R19-03 master 基线记录相同（本机离屏渲染环境）；本 diff cartography 改动仅 schema JSON 键。
- 构建：全量 70% 处 ld 信号 11（宿主症状，同夜 R20-02 同款）→ 定向 10 目标 + CLI 均 EXIT=0；两次 -j1 增量取证构建的偏差已记录（协议 §4 目标增量路径，聚合并发 2 = -j2 包络）。

## ④ 可量化改善

- 取消及时性：不可打断（跑完全程 300 步 ≈3s，真实 GDAL 写盘分钟级）→ 探针轮询即停（测试中 ≈3–10 步内，<1.5s 界）；失败路径误报（Cancelled→modal）新 oracle 封死。
- Surface 契约：registry 腿 17695/17696 → 17868/17868（7 工具 raw=canonical，R19-02 遗留 KNOWN_BASELINE_FAIL 清除）。
- 导出治理：同一 QGIS 导出引擎四条入口（cartography export/atlas、mapspec、layout:export、GUI 对话框）DPI 下界全部一致 [72,1200]。

## ⑤ 独立评审

- R1（fresh reviewer #1）：APPROVE-WITH-FIXES（7 发现）→ 6 修 2 记录（edc7c27e17）。
- R2（fresh reviewer #2）：**APPROVE**（0×P0/P1/P2，5×P3）→ 4 修 1 声明（8d41eecd3c），受影响套件复跑 ×2 全绿。
- 剩余风险：seam 无生产调用者（契约先行，guardrail 保持关闭）；CP11 环境红待带显示环境复核（#1655 归属决策留维护者）。

## ⑥ 交付

- PR：https://github.com/WindWang2/exp-rs/pull/1672（非草稿）
- HEAD：`8d41eecd3c`（BASE 28e1891390）
- Worktree：`exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`
- 账本最后记录：LOOP_LEDGER Round 7（本文件同批提交）。
