# ZCode R20-04 LOOP_LEDGER

## Round 0 — LIVE RECON + 工作树（2026-10-23 02:50–03:20 CST）
- master=28e1891390（fetch 后复核）；无 open PR；R20-01/02/03 worktree 状态与文件域记录（BASELINE.md）。
- worktree `exp-rs-agent/r20-04-qt-qgis-cartography-20261022T190613Z`，分支同名，基于 origin/master。
- 构建配方：系统依赖（R20-02/03 已证）；全局锁 -j2；构建任务入队（队列第 4 位：R20-02 持锁 56%+ → R20-03 → 未知 → 本轨）。
- 关键证据：#1560 现码确认未修；CT-6 7 工具 raw schema 缺项逐一静态确认；DPI 双界（layout_service.cpp:1360 [1,1200] vs export.cpp:158 [72,1200]）确认；W1b 双路径身份对照完成（CROSS_TRACK_FINDINGS #1）。

## Round 1 — RED 种子（d5b95a9546）
- test_raster_processing_dialog_base：prompt-cancel oracle SECTION（master 确定性红：steps==300）。
- test_layout_tools：dpi 36 拒绝 SECTION（master 红：放行并渲染）。
- CT-6：复用既有 test_surface_snapshot registry 腿为 RED（R19-02 已证）。

## Round 2 — 三个修复（f9bbe9a5f6 / 7e147f0361 / b7a04d7465）
- #1560：runGdalTask 签名 → std::function<QString(const GdalTaskCancelProbe&)>，探针绑 ctx.isCancelled()；新增 static gdalCancelProgress（GDALProgressFunc 适配器，取消时返回 0 中止）；测试更新 + 探针真值表 SECTION。
- CT-6：4×properties + 3×type 一行式补齐；全仓 inputSchema 扫描确认无其他非规范 raw（helper 型工具均输出双键；1048 块为扫描误报）。
- DPI：exportLayout 下界 1→72，注释说明对齐治理链；NaN-safe 形式保留。
- 快照验证：7 工具在快照中均已是 canonical 双键（修 raw 后 registry 腿应绿，无需 regen）。

## Round 3 — #1632 triage（ISSUE_TRIAGE.md）
- 条目 1/2/5 上游已修（证据在案）；条目 3 判定不改（测试使用中，"无使用者"过期）；条目 4 移交（作业提交热路径重构，避免混入本 PR）。

## 待续（构建完成后）
- Round 4：RED 取证（在 d5b95a9546 上增量重建跑 3 个 oracle）→ Round 5：HEAD GREEN×2 + 回归族 → Round 6：R1/R2 评审 → Round 7：PR。

## Round 5 — R1 独立评审 + 修复（edc7c27e17）
- fresh reviewer 子代理全 diff 评审 → APPROVE-WITH-FIXES（7 发现：2×P2、5×P3；REVIEW_R1.md）。
- 已修 6 项：modal guard、promptness <150 界、gdalCancelProgress 异常安全、头文件 local-copy 文档、GUI 导出 min 72、resultAnnounced 真实化。
- 不改 2 项（5a 快照漂移风险；7 范围诚实记入 PR 正文）。
- Reviewer 独立证实：CT-6 修复完备（122 工具/36 schema 体零 diff）、探针线程安全、DPI 无回退面。

## Round 6 — 全量证据闭环（2026-10-23 07:30–08:30）
- 构建：全量 70% ld 段错误（test_spectral_anomaly，宿主症状同 R20-02）→ 定向 10 目标 BUILD2_EXIT=0 → CLI BUILD3_EXIT=0；两次 -j1 增量（偏差记录于 TEST_RESULTS：R20-01 持锁 -j1 期间，聚合并发 2 = 协议 -j2 包络）。
- RED（种子 d5b95a9546 实跑）：O-1 `300 < 300`（:160）；O-3 `!true`+`false`（:774/:775）；O-2 chart_list schema 失配（:322）。CT-6 另有 master 二进制预取证（6/7 用例、唯一失败 registry 腿）。
- GREEN×2（HEAD edc7c27e17）：10 个二进制两轮全绿（119/17868/5506/1311/29/132/2830/9956/169 + CP11 25/28×2 环境红同款）。
- KNOWN_BASELINE_FAIL：CP11 :811/:1160/:1296 与 R19-03 master 记录逐位一致。

## Round 7 — 待办
R2 独立评审 → push/PR。

## Round 7 — R2 评审 + 收尾（8d41eecd3c）
- R2（独立 reviewer #2）：APPROVE（0×P0/P1/P2，5×P3）→ 修 4（modalSeen oracle、精确 DPI 错误串、探针生命周期文档、OWNERSHIP 表）+ 声明 1（seam 生产态观察）；受影响两套件 ×2 复跑 120/1311 全绿。
- .gitignore append-only 白名单（r19 同款 3 行模式）+ 全部账本随 PR 入库。
- 终态：6 commits，RED×3（确定性）/GREEN×2×9 二进制，双轮独立评审通过，准备 push + PR。

## Round 8 — 交付闭环（2026-10-23 09:1x）
- push 前终检：master 未动（28e1891390）；4 个兄弟轨 open PR（#1668–#1671）changed-files 与本轨零源文件交集（仅 .gitignore 各自 append-only 块）；merge_loss_audit 223 条历史告警中 0 条涉及本轨文件；git diff --check CLEAN。
- 推送 + PR：**https://github.com/WindWang2/exp-rs/pull/1672**（非草稿，base=master，head=129c37aaca）。不自动合并、不等 CI。
