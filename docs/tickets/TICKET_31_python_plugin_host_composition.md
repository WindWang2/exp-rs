# Ticket TICKET-31: Python 插件加载原生组合

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_cli_pipeline_runner_consolidation.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_cli_pipeline_runner_consolidation.md)

## 问题 (Question)

如何在 `RsPipelineRunner` 中直接组合 `PythonPluginHost`？

## 决议 (Resolution)

在 `RsPipelineRunner` (`src/cli/rs_pipeline_runner.h`) 中：
1. 新增 `bool addPythonPluginDirectory(const std::string& dirPath, std::string* errorOut = nullptr)` 接口。
2. 内部惰性创建 `PluginHost` / `PythonPluginHost`，并自动调用 `loadPlugin`。
3. 在执行 `runFromJson` 或 `runFromFile` 前，确保所有配置的 Python 插件已被加载并注册至算法注册表。
