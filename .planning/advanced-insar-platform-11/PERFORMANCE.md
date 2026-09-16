# PERFORMANCE — 资源模型与逻辑规模

## 硬约束（GOAL）

- `CMAKE_BUILD_PARALLEL_LEVEL=2`；Ninja/CMake build `-j2`（压力高降 `-j1`）；禁止 `-j$(nproc)`。
- `CTEST_PARALLEL_LEVEL=1`；测试 `-j1`；`QT_QPA_PLATFORM=offscreen`。
- Git Bash 下负载平均不可测 → 记 not-executed 一次，构建恒 `-j2`（EVIDENCE.md 已记）。

## 各模块逻辑资源模型（构造级，非 wall-clock）

| 模块 | 内存模型 | 上限/拒绝 |
|---|---|---|
| sar_baseline | O(1) 状态向量 + O(states) 轨道段 | 轨道状态数 ≤ 100000（解析拒绝） |
| sar_topographic_phase | 流式行块：O(demCols + tile)；每像元 2×forwardRangeDoppler（每次 O(states)） | DEM 行块化；无整平面物化 |
| sar_coregistration | patch 格点 O(lattice) + warp 行块 O(cols) | patch 网格数上限 4M（拒绝） |
| sar_unwrap builtin | 既有 2 GiB 平面预算 + 队列 4·像素 | 既有 MEMORY_BUDGET_EXCEEDED |
| sar_unwrap provider | 中转 raw float32 文件 w·h·4B ×2 + QProcess | timeoutSec 缺省 600；临时文件退出即清理 |
| sar_pair_network | O(scenes²) pair 表 | scenes ≤ 512（拒绝）；pair ≤ 65536（拒绝） |
| sar_network_inversion | 正规矩阵 O(nepochs²)（nepochs ≤ 200 拒绝）；pattern 缓存 ≤ 128（拒绝超出）；像元流式 | 同左 |
| 相位闭合 | O(3·tile) 流式 | — |

## 观测（回填）

-（每 Phase 构建期间记录 CPU/RSS；测试记录峰值 RSS 若可行）
