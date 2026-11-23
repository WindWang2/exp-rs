# 产品内 Agent 基础（Vendored Pi）

对照日期：2026-11-23（UTC+8）。对照提交：`master@d9920efafe80`（合入于 2026-11-21 00:15 UTC+8）。

本文是产品内 AI 副驾驶的现行入口。撰写于 2026-11-15 的实施方案当时标注“尚未实施”；P0–P3 现已合入 master，P4 的采样与确定性契约已落地，真实云端模型与最终 Windows 可见验收仍未完成。在 P5 可见发布通过之前，**不会**把 Pi 设为正式默认后端。

## 职责划分

| 职责 | 所有者 |
| --- | --- |
| 通用模型循环、流式输出、工具调用循环、对话上下文、会话压缩 | 随包 Node + 锁定的 Pi SDK 1.5.2（`vendor/pi`，由 `pi/product-runtime` 组装） |
| 模型配置与凭据持久化、双语 UI、工作区策略、审批、任务执行、取消、结果验证、资产交付 | Qt 宿主：`LlmConfigManager`、`AgentCopilotDockWidget`、`AgentToolHost`、Task Center、`ToolCallDispatcher`、`copilot_tool_gate`、`OutputCommitter` |
| 科学会话的预算、预检、计划验证、恢复与审计 | 原生 `src/agent_loop/scientific_agent_session.*`（ADR 0175 / AgentOps），不被通用 LLM 循环替换 |

产品用户不必安装 Pi、Node 或 npm。Qt 不嵌入 Node，也不嵌入 Pi 终端界面。

## 工具必须围绕宿主代理

Pi 子进程只注册宿主提供的工具定义，并通过私有协议 `sicnu.pi/1` 把调用交回 `AgentToolHost`。首版明确关闭 Pi 自带 Shell、通用文件读写、任意扩展和 Codemode（见 `pi/product-runtime/controlled-session.mjs` 的 `EXCLUDED_TOOLS` 与 `noTools: "all"`）。批准后的 Python 必须走已锁定的 `vendor/python/` 与现有 Dispatcher 事务提交，不得另起执行权威，也不得让脚本绕过宿主。

同一工程同时只允许一个有副作用的 Agent 写会话（宿主租约）。人工 Task Center 任务不受此限制。一个产品对话对应一个 Pi 会话。不引入 Pi 持久调度器。

`search_tools` / `get_tool_schema` 只检索当前宿主快照中已准入的工具（会话注册上限 64）。完整 Processing / 独立 MCP 目录仍供工具箱和外部 `--mcp` 使用，不等于产品会话的可调用集合。

## 两条 Pi 集成，不要混用

1. **产品内循环（桌面副驾驶的基础）**：`pi/product-runtime` + `src/agent/runtime/pi_process.*` / `pi_conversation_runtime.*` + `AgentToolHost`。不启动 `exp-rs --mcp`。
2. **外部 Pi CLI 扩展（ADR 0122）**：`pi/exp-rs-spatial.ts` + `pi/mcp_bridge.ts` 拉起独立的 `--mcp` 进程。该进程使用另一套内存中的 DataManager / Task Center，**不能**当作产品 GUI 的执行权威。

当前 Dock 仍提供 Native 回退（默认），Pi 需在副驾驶中显式选择。按原方案，回退只允许发生在尚未提交任务的新会话或启动阶段，不能在进行中的轮次静默换引擎。

## 文档与后续

- 实施方案：`docs/superpowers/plans/2026-11-15-vendored-pi-product-agent.md`
- 阶段记录：`implementation-ledger.md`
- 上游核实：`docs/research/2026-11-15-vendored-pi-agent-upstream.md`
- 历史 ADR 0122 保留当时关于上游 MCP 能力的决策上下文；现行约束以本文和产品运行时为准。上游现已提供内置 MCP 与 Codemode，产品首版故意不启用。

仍待后续阶段（需用户授权，本对照不视为已完成）：

- P4：真实 StepFun（或其他已配置云端模型）采样，与确定性 fixture 分开报告
- P5：从合并后 master 打出的 Windows 包，在无系统 Node/npm/Python 的环境下做可见双语交付
- 可见验收通过后，再把 Pi 设为正式默认，并保留一个迁移周期的 Native 回退
