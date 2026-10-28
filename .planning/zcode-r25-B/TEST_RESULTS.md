# R25-B TEST_RESULTS

## RED 证据（BASE=2610cb4741，2026-10-28 采集）

方法：BASE detached worktree + HEAD 测试 TU + BASE 源码 include 路径，`-fsyntax-only`，生产编译旗（取自同库现有 TU 的 compile_commands）。

| 测试 TU | BASE 编译退出码 | 首个错误 |
|---|---|---|
| test_r25_b_import_preflight.cpp | **1** | `data/import_preflight.h：没有那个文件或目录` |
| test_r25_b_inference_plan.cpp | **1** | `operators/runtime/inference_plan.h：没有那个文件或目录` |
| test_r25_b_replay_deviation_v2.cpp | **1** | `‘ReplayDeviationEntry’不是一个类型名` + `no member named ‘deviationDetails’` |

HEAD（本轨分支）同旗语法检查：6 个 TU（3 生产 + 3 测试）+ E2E TU 全部 **exit 0**。

## GREEN（待构建完成后填写）

- [ ] `ctest -R "r25_b"` 第一轮
- [ ] `ctest -R "r25_b"` 第二轮
- [ ] 邻域回归（r23_b/r24_b/data_manager/experiment/model_runtime/mlops9）
- [ ] E2E

## 构建环境备注

共享宿主：R25-A/R25-C 并行构建持锁排队；`CMAKE_BUILD_PARALLEL_LEVEL=2`、flock 单锁、ctest -j1。构建命令与退出码在 GREEN 段记录。
