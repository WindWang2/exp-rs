# Wayfinder Map: Consolidate CLI Pipeline Execution into RSPipelineRunner Deep Seam

## 目标 (Destination)

深化 `RsPipelineRunner` (`src/cli/rs_pipeline_runner.h`)，使其直接组合 Python 插件加载、自动 `DataManager` 管理与 TaskCenter 流水线调度，为 `sicnu_geo_rs_cli` 主程序及无界面（Headless）测试提供单一、高杠杆、零样板代码的流水线执行深层 Seam。

## 说明 (Notes)

- **领域词汇**: 参考 [CONTEXT.md](file:///home/kevin/projects/exp-rs/CONTEXT.md) 中的 RS Pipeline Runner, Task Center, Data Manager, Data Asset 术语。
- **架构词汇**: 运用 `/codebase-design` 深度模块术语 (**module**, **interface**, **depth**, **seam**, **adapter**, **leverage**, **locality**)。
- **相关 ADR**: ADR 0016 (TaskCenter Pipeline Execution), ADR 0023 (CLI Pipeline Step Asset Registration).

## 决策记录 (Decisions so far)

- [TICKET-30: 目标与边界声明](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_30_pipeline_runner_destination.md) — 确定 `RsPipelineRunner` 深化重构的目标与边界。
- [TICKET-31: Python 插件加载原生组合](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_31_python_plugin_host_composition.md) — 在 `RsPipelineRunner` 中新增 `addPythonPluginDirectory` 接口并自动组合 `PluginHost` / `PythonPluginHost`。
- [TICKET-32: DataManager 惰性接管与资产注册](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_32_lazy_data_manager_ownership.md) — 内部惰性创建或绑定 `DataManager` 实例，自动接管中间步骤资产注册。
- [TICKET-33: main_cli 样板代码消除与端到端验证](file:///home/kevin/projects/exp-rs/docs/tickets/TICKET_33_main_cli_boilerplate_elimination.md) — 极简化 `main_cli.cpp`，清理分散的手动装配逻辑，并通过 CLI 集成测试验证。

## 待确定事项 (Not yet specified)

- 无。

## 超出范围 (Out of scope)

- 修改 `TaskCenter::submitPipeline` 的 DAG 调度算法。
- 修改 GUI 桌面主窗口的流水线图形化编辑器逻辑。
