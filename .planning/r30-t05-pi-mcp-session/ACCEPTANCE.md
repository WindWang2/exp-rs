# R30-T05 ACCEPTANCE — 完成条件 ↔ 证据映射

## 轨道完成条件逐条对照

### 1. 原 #1827 能力无回退；stale schema 不再静默 dispatch；unchanged 工具可继续；wait/error/abort/deadline 语义由行为回归证明

- 无回归：全套件 84 tests / 83 pass / 0 fail / 1 skip（原 66/65/0/1 + 新增 18）；2 个旧断言按新契约更新（DECISIONS D2，任务指定的行为变化）。
- stale dispatch 拒绝：`pi/test/r30_t05_drift_dispatch.test.mjs` 6/6 GREEN（changed/removed 拒绝、pending 等待、UNVERIFIED 可重试、失败 comparison 重试恢复、两次重启恢复、unchanged 继续）。
- wait 语义：`pi/test/r30_t05_wait_semantics.test.mjs` 8/8 GREEN（isError 透传、timeout 校验、预算含在途请求、abort 不被迟到终态复活、cancel 恰一次、malformed 快败、cancelling 非终态）。
- 状态：**DONE（Pi 侧）**。

### 2. 大结果说明与实际 continuation 一致；至少一个真实 artifact 经 Pi 分页完整恢复并校验；无虚构 ref

- 单元：`r30_t05_details_continuation.test.mjs` 4/4 GREEN（信封提取、嵌入 JSON 不可伪造、CJK、structuredContent 预算）。
- 真实 host 分页恢复 + sha256 重组：`r30_t05_e2e_real_host.test.mjs` E1（待 build 后运行）。
- 状态：**单元 DONE；E2E 待构建验证**。

### 3. session 状态、journal、reconnect、repair 资格、Control Center、SDK 接缝四组场景

- E1 真实结果链：`r30_t05_e2e_real_host.test.mjs`（发现/inspect/执行/wait/分页；待运行）。
- E2 长任务控制：同文件 E2（abort 服务端取消、timeout 不取消、host 存活；待运行）。
- E3 重启与 durable 证据：`r30_t05_e2e_restart_durable.test.mjs`（A: surface host kill→gate→checkpoint→resume；B: 生产 binary UNKNOWN_SESSION + reconcile 判定；待运行）。
- E4 AgentOps+SDK：现有回归（dock_lifecycle、approval_gate、agent_ops_ui_smoke、plugin 全家、harness 证据矩阵）+ r29 driver 用例（build2 运行）。
- 状态：**待构建后执行**。

### 4. 本轨构建、直接受影响测试、完整场景通过；不可执行通道有阻塞记录；独立 review 收口、PR 提交

- 构建：build1 进行中（-j4 双锁）。
- C++ 测试：test_r30_t05_journal_read_bound（WP06）、test_mcp_server（+evict 用例）、test_r29_t03_production_driver、test_agent_loop_*、test_agent_ops_*、test_cli_agent_ops、test_agent_session_adapter、WP07/08/09/10 清单（build2）。
- 独立 review：WP12 R1（新上下文子 agent）→ 修复 → R2 → PR。
- 状态：**进行中**。

## 状态词汇

DONE / ALREADY_DONE_WITH_EVIDENCE / BLOCKED / NOT_EXECUTED — 每 WP 在 LEDGER.md 记账。
