# PERF_BASELINE — R23-E

## 结论：N/A（无生产热路径改动）

本轨交付全部为「回归测试 + 工程门禁脚本」，生产 src/** 零修改（见 ORACLES.md 声明）。因此：

- **运行时性能**：无基线/对比可测——没有任何被交付物在生产执行路径上运行（ctest/CI 阶段工具除外）。NO_MEASURABLE_BENEFIT 且无可回归面。
- **构建性能**：新增 3 个测试目标 + 2 个 ctest 脚本门。对默认构建影响 = 3 个测试 TU 的编译（小）；对 CI 的影响为三个轻量门（integrity ~2s、python typed ~3s、native terminal ~1s，实测见 TEST_RESULTS）。
- **不做**未经量测的任何"提升 X%"声明。

## 量测记录（测试自身的运行成本，供 CI 预算参考）

| 项 | 实测（WSL archlinux, python3.14/numpy 2.5.4） |
|---|---|
| check_merge_registration_integrity.py（git 模式） | <2 s（1038 目标双向提取 + git grep） |
| test_r23_e_python_worker_typed.py（5 用例，真 daemon 拉起×N） | 2.9 s（Ran 5 tests in 2.862s） |
| test_r23_e_native_terminal.cpp（3 用例） | 待构建后填 |
| test_r23_e_plugin_typed_gates.cpp（2 用例 × 7 变体 spawn） | 待构建后填 |
