# Ticket TICKET-33: main_cli 样板代码消除与端到端验证

- **类型**: `task`
- **状态**: 已关闭 (Closed)
- **父级地图**: [MAP_cli_pipeline_runner_consolidation.md](file:///home/kevin/projects/exp-rs/docs/tickets/MAP_cli_pipeline_runner_consolidation.md)

## 问题 (Question)

如何清理 `src/cli/main_cli.cpp` 中的散落代码并通过测试验证？

## 决议 (Resolution)

1. **`main_cli.cpp`**：
   - 简化命令行解析后对 `--python-plugin` 的处理逻辑：直接循环调用 `runner.addPythonPluginDirectory(dir)`。
   - 移除在 `main_cli.cpp` 中手动声明与传递 `std::unique_ptr<PythonPluginHost>` 及 `std::unique_ptr<DataManager>` 的分散代码。
2. **测试验证**：
   - 在 `tests/test_pipeline_runner.cpp` 中运行包含 Python 插件与 C++ RS 算子的混合 pipeline 端到端测试，验证执行、打印与资产注册完全正常。
