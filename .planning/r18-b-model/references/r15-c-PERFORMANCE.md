# R15-C Performance — 真实测量记录（C15）

> 只列真实测量；未运行的硬件/平台测试明确标注 NOT VERIFIED。
> 方法：`test_model_runtime_bench`（env-gated `SICNU_MODEL_BENCH=1`，opencv-dnn
> 后端 + 仓库 fixture `tests/data/test_infer_identity.onnx`，512×512×4 合成
> 栅格，tile 64）。测量项：冷/热 session 装载、tiled 吞吐、取消延迟、峰值 RSS。
> 环境：Windows，MSVC 2022 Debug，`build-r15-c`，单测试进程运行。

## 测量结果（同宿主、同一天、各一次完整运行）

| 指标 | master（46791bc6，build-dev） | 本轨（hardening/r15-C-model-runtime） | 判定 |
|---|---|---|---|
| 冷 session 装载 (ms) | 35.37 | 30.92 | 持平（略优，单次测量噪声内） |
| 热 session 装载 (ms) | 4.75 | 4.20 | 持平 |
| tiled 吞吐 (tiles/s) | 154.62 | 179.48 | 持平（略优） |
| tiled 吞吐 (px/s) | 633328 | 735165 | 持平（略优） |
| 多输入吞吐 (tiles/s) | 123.93 | 120.85 | 持平（噪声内） |
| 取消延迟 (ms) | 6.54 | 4.28 | 持平（略优） |
| 峰值 RSS (MiB) | 88 | 88 | 持平 |

原始基线文件由 bench 写出（`benchmarks/model-runtime-4.json`）；两棵树的该文件已
还原，不提交机器相关基线更新，数字以上表为准。

## 热点优化

本轮**未实施热点优化**（C15 Oracle："只有有证据才实施热点优化"）。本轨改动全部为
正确性/容错/可诊断性；性能敏感路径（acquire/evict/tile loop/decode）的改动均为等
复杂度或严格减少浪费（能力预检把多输入失败从 IO 之后前移到 IO 之前，实际减少
无效 forward 与栅格读取；publish fold 仅在崩溃后缀状态触发，热路径零成本）。

## NOT VERIFIED

- CUDA / TensorRT / OpenVINO / ONNX Runtime 执行车道（本宿主无依赖与硬件）。
- Linux 基线；真实大规模遥感数据（使用小合成数据等价验证）。
- p50/p95 分位数：bench 输出单次汇总值（tiles/s、ms），未做多次重复采样；
  报告的是单次完整运行的真实值，非外推。
