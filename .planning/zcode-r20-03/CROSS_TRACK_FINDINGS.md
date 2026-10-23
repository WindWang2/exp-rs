# R20-03 CROSS_TRACK_FINDINGS

## CT-A [04 轨] surface 快照 registry 腿 —— R19-02 CT-6 存续确认（live）
- 7 个 cartography 工具 raw schema 缺项（4 个缺 `properties:{}`：catalog_index/chart_list/list_rules/list_token_sets；3 个缺 `type:"object"`：diff_templates/export/explain）。
- 本轨以 tests/test_surface_snapshot registry 腿的确定性失败作为跨轨证据（失败信息含工具名）；预期 KNOWN_BASELINE_FAIL 直至 04 轨补 canonical 形态。
- 本轮证据：见 TEST_RESULTS.md（构建后采集）。

## CT-B [CLI 灰区] CLI 人工审批门不可达（新发现 D9）
- `src/cli/cli_agent_ops_commands.cpp:145-153`：`--approve` 映射 `args["approve"]`（bool）——全库零消费者（grep 实证），静默 no-op；`--approve-pending-repair` 映射 `args["approve_pending_repair"]`——被 #1539 修复后的 driver 明确拒绝（APPROVAL_TOKEN_REQUIRED，这是**有意的**拒绝）；CLI **没有** `--repair-approval <token.json>` / `--approval-now-ms` 通道。
- 结果：CLI 表面上提供审批标志，实际上人工修复审批在 CLI 面永远无法武装——用户要么被拒（拼写法）要么被静默忽略（--approve）。
- 归属：src/cli 为本轨灰区（r18-c 先例列为只读）；建议由 CLI 相关轨把 `--approve` 改为要求令牌文件并映射 repair_approval/approval_now_ms，或删除误导性标志。本轨只在 agent_ops 域保证：`session approve_repair` 动作与 MCP 路径令牌可用（已由既有测试守护）。

## CT-C [02 轨/共享] 无界文件读的跨轨邻居（D6 部分项）
- `src/preflight/capability_mirror.cpp:293`（本轨写区，但镜像文件由 preflight/processing 域共同消费）与 `src/workflow/workflow_run_lock.cpp:51,167`。种植大文件 → 无界读。与 checkpoints 路径已有的 bounded-reader 模式不一致。建议后续轮统一（本轨 DEBT D6 记录）。
