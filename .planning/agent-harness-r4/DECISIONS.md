# DECISIONS — Track 8 R4

D1（Phase 0）**WP 前提修订**：run_loop/ContextLedger 的任务书心智模型与实测不符（证据见 BASELINE.md §6）。按任务书"前提不成立→记账后收窄"规则重锚定，不跳过任何 WP——每个 WP 在真实接缝上保留了原对抗意图。

D2（Phase 0）**在途 PR 规避**：不改 `grounding_probes.cpp`（#1337）、`lab_copilot.cpp`（#1335）、`mcp_server.cpp`（#1334）、`src/agent/CMakeLists.txt`（#1335）。实现改动集中在无重叠文件：`harness_verification.*`、`agent_plan.*`、`context_ledger.*`、`evidence.*`、`context_checkpoint.*`、`llm_streaming_client.*`（#1334 未触及，rg 实证）。

D3（Phase 0）**构建策略**：lab 档强制 ENABLE_TESTS=OFF 不可用；标准配置 + 点名构建本车道目标（`ninja -j2 <targets>`），Oracle 的 ctest -R 过滤器只覆盖本车道。全仓 2000 目标全量构建不属本轨道验收面。

D4（Phase 0）**邻接领地只测不改**：`tool_call_dispatcher`（src/processing）、`workflow_checkpoint`（src/workflow）、`mcp_server`（#1334 在途）——测试可链接其库（先例 test_verification_failure_11），实现不动；"三未知工具词表并存"记 backlog 不在本轨道统一。

D5（Phase 0）**token 记账**：口径见 PLAN.md 末节——subagent 用量精确、主线估算、不为凑 280M 注水。工作量门禁以 3.2 交付下限（8 类/20+ 用例/10 提交/12 文件/语料复用）为硬闸。

D6（WP-A）**SSE 可观测性最小实现**：截断/坏 JSON 工具调用从 qWarning-only 升级为类型化 Qt 信号 `malformedToolCall(QJsonObject)`（{reason, finish_reason, name?, error}），不改既有 toolCallParsed 语义（纯新增，向后兼容）；参数/缓冲上限沿 llm_streaming_client 既有常量风格新增。

D7（WP-A）**自相矛盾探针最小实现**：计划级同输出名冲突（两 step 对同一产出声明互斥主张）→ `validateAgentPlan` 增一条 issue。码与既有全部 validateAgentPlan issue 一致用 `kInvalidPlan`（该函数现状所有 issue 均为此码，一致性优先），summary 携带 "Duplicate output name"。数值互斥的通用一致性检查不做（无合同来源，会变成猜测式启发）——本探针的合同来源是"一个产出路径至多一个生产者"的 workflow 语义。

D8（WP-F）**计划级大小上限**：步数 ≤4096、计划文档 ≤4MiB，超限 INVALID_PLAN。合同先例：HarnessSessionStore kMaxDocumentBytes。上限值写入 tests 与头注释作漂移锚。
