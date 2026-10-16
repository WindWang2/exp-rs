# R15-C Performance — 真实测量记录（C15）

> 只列真实测量；未运行的硬件/平台测试明确标注 NOT VERIFIED。
> 方法：`test_model_runtime_bench`（env-gated `SICNU_MODEL_BENCH=1`，opencv-dnn
> 后端 + 仓库 fixture `tests/data/test_infer_identity.onnx`，512×512×4 合成
> 栅格）。测量项：冷/热 session 装载、tiled 吞吐、取消延迟、峰值 RSS。
> 环境：Windows，MSVC 2022 Debug，`build-r15-c`，`-j2` 构建；单测试进程运行
> （`CTEST_PARALLEL_LEVEL=1`）。

## 测量（待构建完成后填写）

| 指标 | 基线（master @ 46791bc6） | 本轨（hardening/r15-C-model-runtime） | 判定 |
|---|---|---|---|
| 冷 session 装载 (ms) | 待测 | 待测 | — |
| 热 session 装载 (ms) | 待测 | 待测 | — |
| tiled 吞吐 (tiles/s, px/s) | 待测 | 待测 | — |
| 取消延迟 (ms) | 待测 | 待测 | — |
| 峰值 RSS (MiB) | 待测 | 待测 | — |

## 热点优化

本轮**未实施热点优化**（C15 Oracle："只有有证据才实施热点优化"）。本轨改动
全部为正确性/容错/可诊断性；性能敏感路径（acquire/evict/tile loop/decode）
的改动均为等复杂度或严格缩小工作量（如能力预检把多输入失败从 IO 之后前移到
IO 之前，实际减少浪费的 forward 与栅格读取）。

## NOT VERIFIED

- CUDA / TensorRT / OpenVINO / ONNX Runtime 执行车道（本宿主无依赖与硬件）。
- Linux 基线。
- 真实大规模遥感数据（使用小合成数据等价验证，未下载大数据集）。
