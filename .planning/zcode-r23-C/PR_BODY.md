# r23-C: Pi/MCP Harness、Qt/QGIS 工作台与制图交互闭环

**BASE** `ad98a7a704` (origin/master, 2026-10-26) → **HEAD** `<本 PR 分支>`
三轨并行（r23-A/B 同时执行，全局构建锁互斥实测有效）。

## 结论（Evidence-first）
对 144 个 open issues 做了双 subagent + 主会话读码 triage（`ISSUE_TRIAGE.md`）：R20-R22 合并波已修复绝大多数在列缺陷（含 C06 指定复核的 #1539/#1540、C04 的 #1552、C03 的 #1628-1/#1650 等，均 FIXED_OPEN，证据在文档）。确认 LIVE 且实施的三项：

### 1. TaskCenter 累积/增量日志索引空间统一（C05；R22-agent REVIEW_R2 R2-F3 + R2-F6 收口）
`#1705` 有界日志保留后，消费端 cumulative 分支仍以 retained 窗口 `logLines.size()` 对比 delta 分支的**绝对**索引 `m_forwardedLogCounts`：chatty 任务越界后，引擎内部 append（"Cancelled due to shutdown"类）永不投递（R2-F6）；乱序投递下错切片→丢行→重行（R2-F3）。修复按 `JobRecord::logLinesOffset` 契约在消费端做绝对空间翻译（`droppedLogLines + size`，切片自 `max(forwarded, dropped)`）；无 eviction 时与旧行为逐字节等价。
- **RED**：`test_r23_c_task_center_log_spaces`（新，经 `deliverJobRecordForTests` seam 受控乱序注入真实形状记录）两用例各 `0 == 1` 失败
- **GREEN×2**：24 断言/2 用例 ×2 轮；`test_task_center` 533/34 ×2、`test_r22_agent_job_logs` 24031/4 ×2 无回归

### 2. 成绩册 JSON/CSV 成对原子发布（C10；#1695-17e）
`publishBatchOutputsAtomic` 先发布 JSON 再构建/写 CSV——第二工件失败（.csv 名被目录占据、ENOSPC）留下「新 JSON + 旧 CSV」的分裂对。现改为：两载荷先构建→目录占位预检→双双 staging（sidecar 权威）→经 `atomic_fs::publishStagedMembers`（journal + .bak 回滚）发布。
- **RED**：`test_r23_c_teaching_pair_publish`（新）失败发布后 JSON 仍被新内容覆盖（展开证据）
- **GREEN×2**：25/3 ×2；批量回归 `test_teaching_batch_r4` 77/7、`test_r18_e_batch_checkpoint` 33/3、`test_r15_E_batch_refusal_accounting` 62/9 全绿

### 3. 发现拒绝计数的 %n 复数形式（C09；#1695-17b）
"entr%2" y/ies 拼接改为 Qt 复数形式（`%n` + disambiguation），与 QGIS core 先例一致；旧串无翻译条目，零翻译回归面。

## 测试与门禁
- 终门禁：`ctest -R 'r23_c' --parallel 1` → **100% (3/3)**；`git diff --check` 干净；零冲突标记
- 交叉验证：事故提交 f878807475 曾意外回退修复②，测试立即变红、恢复后回绿——双向证明测试真实触发
- 工具链：GCC 16.2.1 / cmake 4.4.4 / Debug / -j2 + 全局 flock

## 双独立评审（GitHub 无正式 Review 对象，证据在 `.planning/zcode-r23-C/REVIEW_R1/R2.md`）
- **R1**（独立 subagent 逐 hunk）：FIX-THEN-SHIP — P2×1（测试锚点竞态）+ P3×6，全部处置（c875659c00）并双跑复核
- **R2**（另一位 subagent，不继承 R1，含 R1 修复复审、Qt 6.8 %n 实证探针）：FIX-THEN-SHIP — P2×1（文档过度声明）+ P3×5，处置（9b41635399）/记录（RISK_REGISTER R-9/R-10）
- 评审独立复核结论：WP-1 交错数学 exactly-once 成立；WP-2 全部失败路径要么旧对完整要么一致新对；共享文件纯 append

## Issue 状态（不代关）
- 本 PR 修复：#1695-17e、#1695-17b（+#1705 后续 R2-F3/F6 收口）
- FIXED_OPEN 证据（供人工复核关闭）：#1539 #1540 #1552 #1558 #1570 #1571 #1573 #1574 #1628-1/2/4/5 #1632-1/2/4/5/6 #1639 #1640 #1646-1/4 #1650 #1652 #1653 #1654 #1693 #1694 #1689 #1678 #1688 #1704 #1690 #1691 #1692 #1705 等
- 转记未修（超最小修复预算/权属外）：#1695-17c（GUI 线程发现）、#1632-3（exp_gui 重复类）、#1655（CP11 渲染门）、#1646-2（形式 UB）、R2-F2（geospatial 权属）

## 重现
```bash
cmake -S . -B build-r23-C -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
flock "$HOME/.cache/exp-rs-build.lock" cmake --build build-r23-C --parallel 2 \
  --target test_r23_c_task_center_log_spaces test_r23_c_teaching_pair_publish test_task_center test_r22_agent_job_logs
QT_QPA_PLATFORM=offscreen ctest --test-dir build-r23-C --output-on-failure --parallel 1 -R 'r23_c'
```

不等待 CI、不自动合并。规划/评审/风险全档：`.planning/zcode-r23-C/`。
