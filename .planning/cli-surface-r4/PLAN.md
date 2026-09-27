# PLAN — Track 14: CLI Surface Completion R4

预算表见任务书 3.3（8 Phase / 280M tokens）。本文件记录接缝级执行序；逐轮细节进账本。

## Phase 0（16M）— 基线 ✅ 进行中
- [x] worktree `../exp-rs-cli-surface-r4`（分支 `hardening/r4-cli-surface` ← `origin/master=15e5c66b5`）
- [x] 7 open PR 重叠图（§BASELINE.2）
- [x] 29 文件清单 + 19 命令映射表（§BASELINE.3/4）
- [x] 退出码合同实测（exit_codes.h + docs/headless/README.md）→ DECISIONS D1
- [x] 词表真相实测 → DECISIONS D2
- [x] dispatch 无异常边界 + dataset fail() 恒 1 实锤（§BASELINE.5）
- [ ] 基线 ctest 红绿分布（等 build-gcc15 首建完成）→ 回填 §BASELINE.6
- [x] BASELINE.md / DECISIONS.md / PLAN.md / 账本初始化

## Phase 1（46M）— WP-A 前半 + WP-B 前半
- Tracer Bullet：`run` 用法错误现状取证 → 退出码映射最小修复 → `test_cli_exit_codes_r4` 红转绿
- WP-B：3 条裸异常路径（dataset 缺参/lab 权限/batch 输入缺失）→ 四元组结构化出口 → `test_cli_error_messages_r4`
- 目标：≥10 命令退出码覆盖、≥10 条四元组路径

## Phase 2（44M）— WP-A/B 收尾 + WP-C 前半
- 19/19 退出码 + ≥20 条错误路径
- WP-C：CLI 三级面（19 命令 × 子命令 × 参数）↔ docs/headless/README.md ↔ usage 串双向对账测试（先红后绿）

## Phase 3（40M）— WP-C 收尾 + WP-D 三面
- 词表 diff 清零 + 快照固化测试
- WP-D：≥15 操作 CLI/MCP/GUI 对照（MCP 取证 mcp_server.cpp + tool_catalog；GUI 取证 command_defs/CommandRegistry；落点只在 CLI 侧/文档侧）

## Phase 4（32M）— WP-E/F/G
- WP-E：fail-fast ≥12 用例（副作用零证据 = store 快照前后对比）
- WP-F：dispatch 层兜底异常边界（D6）+ ≥6 注入用例
- WP-G：tests/CMakeLists.txt 收口 + COMMAND_MATRIX.md 19/19

## Phase 5（30M）— 独立对抗 review（1 subagent，只读）→ P0/P1 修复 → REVIEW_LOG.md

## Phase 6（26M）— EVIDENCE.md 双跑日志、SURFACE_PARITY.md、EXIT_CODE_CONTRACT.md、整洁提交（≥20）、push、开 PR（不轮询 CI）

## Phase 7（46M）— 预留：补深 / rebase / 双跑复核。**工作量门禁优先于绿灯**。
