# R28-T03：生产会话 seam 接线（Pi/MCP Harness 真实可执行）

状态：实现落地于 `src/agent/tools/agent_session_adapter.*`；本文描述生产链路与验收面。

## 问题（基线 50cc3255c5）

agent_loop 证据优先循环、planner、preflight、执行引擎、验证、诊断、审批、恢复机制全部存在且
各自有测试，但生产 seam bundle 从未组装：

- `McpServer::setAgentOpsDriver`（src/agent/mcp_server.h）无任何生产调用方 →
  MCP 工具 `scientific:agent_session` 永远 `AGENT_OPS_UNAVAILABLE`（Pi 经 stdio 同样不可达）；
- CLI `defaultCliAgentOpsDriver()` 默认构造（seams 全空）→ 每个 `session run` 返回
  `SEAMS_UNAVAILABLE`（src/agent_ops/ops_driver.cpp 的 seam 门，正确地 fail-closed）；
- `planScientificWork`、`projectPlannerInputs`、`projectPlanToIr`、`ScienceContextBroker::synthesize`
  等引擎只有测试调用方（#1729 记录的缺口在基线仍然成立）。

## 接线（本轨）

`makeProductionSeamBundle()`（src/agent/tools/agent_session_adapter.h）组装六个生产 seam，
全部委托既有引擎、零新科学逻辑、零第二注册表：

| seam | 引擎 | 说明 |
|---|---|---|
| data | grounding probe 链（entity_resolver → spatial:understand，单一缓存） | 每 ref 解析 + 有界事实探测，fact_status 保真 |
| planner | broker.synthesize → projectPlannerInputs → planScientificWork（live capability mirror）→ projectPlanToIr → compileWorkflow | draft.steps 即 lowered 可执行步骤；fingerprint/identity 来自 harness planFingerprint |
| preflight | preflightIntent 规则包（与 harness:execute_plan 同一规则）+ 身份 pin 再验证 | blocked 拒绝执行；输入替换 → IDENTITY_MISMATCH |
| executor | compilePlanToWorkflowJson（确定性重编译）→ WorkflowRunCoordinator::startTrackedPipelineJson | 同一文档预检与执行；取消/超时/状态映射到 loop wire contract |
| verifier | harness::verifyArtifact（既有 HarnessVerifier） | tri-state 封闭词表 |
| diagnoser | run 自身错误记录（步骤错误 + normalizeLegacyError typed 码） | 修复提案仍归 recovery bridge（capability provider） |

入口装配：
- CLI：`defaultCliAgentOpsDriver()`（src/cli/cli_agent_ops_commands.cpp）
- MCP：`main.cpp --mcp` 分支构造 driver 并 `setAgentOpsDriver`
- GUI：Control Center dock 的实例化属 T04 shell（见 .planning/r28-t03-harness-sdk/HANDOFF.md，
  人工审批 mint 入口目前唯一存在于该 dock —— T04 接线后授权路径完整可达）

## 身份与失效（WP02/WP04）

- PlanDraft.bindings（新增可选字段，null=不 pin）：`{inputs: [{name, ref}], pins: {datasets: {slot: …}}}`；
  planner 从 grounding entity 落 pin（注册资产 pin entity id+revision，未注册文件 pin 具体路径）；
  preflight 在规则包之前运行 harness validatePlanIdentity；executor 在信任边界再运行一次。
- 换数据/删数据/revision 前移 → typed IDENTITY_MISMATCH 阻塞（tests/test_r28_t03_production_seams.cpp）。

## 验证聚合 fail-closed（WP06）

`VerificationReport::aggregate`：封闭词表外的 per-artifact verdict（空/大小写不同/未知）聚合为
INDETERMINATE，只有显式 verdict 才能升级为 PASS（此前未知字符串会聚合成 PASS —— 潜在 fail-open，
矩阵测试钉死）。

## 测试与运行

- `ctest -R r28_t03 -j1`（offscreen）：production_seams（真实 rs: 算子端到端 NDVI、缺 NIR 阻塞、
  身份 pin、取消、超时）、verification_matrix、shm_manifest_gates。
- 真实二进制 smoke：pi/ Node 套件（`cd pi && npm test`；`EXP_RS_MCP_BIN=<binary>` 时真实宿主 E2E 生效）。
