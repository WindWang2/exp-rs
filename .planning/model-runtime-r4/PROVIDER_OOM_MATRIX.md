# PROVIDER_OOM_MATRIX — Track 15 WP-E/WP-C 工件

## 一、Provider 开关矩阵（2026-09-27 本机实测）

Provider 代码位置：`src/operators/runtime/{tensorrt_provider,openvino_provider,onnxruntime_provider}.{h,cpp}`
编译门：`SICNU_ENABLE_TENSORRT`（operators/CMakeLists.txt:309）、`SICNU_ENABLE_OPENVINO`（:323）、`SICNU_WITH_ONNX_RUNTIME`（:337）。
本机 SDK 探测：三者的 SDK 头/库均不存在（configure 输出为证）。

| 格 | 配置 | 验证手段 | 结果 | 状态 |
|---|---|---|---|---|
| **off/off（默认格）** | 默认 configure（无任何 provider 开关） | 硬守护测试 `test_provider_guard_r4`（registry typed absence + UnsupportedRuntime typed refusal + 内建 onnx provider 完好）+ Phase 4 过滤面双跑全绿 | configure exit 0；测试绿 | **真实格，PASS** |
| TensorRT ON / 无 SDK | `-DSICNU_ENABLE_TENSORRT=ON`，独立 scratch 构建目录 | configure 级：期望优雅降级不炸配置 | exit 0 + `"TensorRT model provider requested but the SDK was not found"`（configure.log:501 附近）；provider 编译定义未注入 | **真实格，PASS（graceful）** |
| OpenVINO ON / 无 SDK | `-DSICNU_ENABLE_OPENVINO=ON`，独立 scratch 构建目录 | 同上 | exit 0 + `"OpenVINO model provider requested but the SDK was not found"` | **真实格，PASS（graceful）** |
| TensorRT ON / 有 SDK | 本机无 TensorRT SDK | — | N/A（无实证手段；不伪造） | N/A + 原因记录 |
| OpenVINO ON / 有 SDK | 本机无 OpenVINO SDK | — | N/A（无实证手段；不伪造） | N/A + 原因记录 |
| ONNX Runtime | `SICNU_WITH_ONNX_RUNTIME` 默认 OFF，本机无 SDK | 默认格守护测试断言 `hasProvider("onnxruntime")==false` | 覆盖于默认格 | 与默认格合并 |

注：`-DENABLE_TESTS=OFF` 用于矩阵 configure 格（只验证配置面）；测试面全部由默认构建目录承担。
Scratch 目录 `/tmp/r4-matrix-SICNU_*` 已清理。

## 二、OOM 阶梯层级记录（WP-C）

### 真实层级（约束=生产记账本身，降档痕迹=typed plan/session 字段）

| # | 层级 | 接缝 | 触发（真实约束） | 降档痕迹（可观测） | 测试 |
|---|---|---|---|---|---|
| R1 | Admit（基准档） | planTileMemory | 预算 = 峰值估计（逐字节相等） | action=Admit，reason 为空 | test_runtime_oom_ladder_r4 |
| R2 | ReduceConcurrency | 同上 | 预算 -1 字节 | action=ReduceConcurrency + reason（点名 requested/reduced 形状与预算）+ recommendedQueueCapacity=1 | 同上 |
| R3 | Spill | 同上 | RAM 最小形状仍超预算 + scratch 预算真实 ≥ spillNeed | action=Spill + spillBytes + reason | 同上 |
| R4 | Refuse（typed 异常） | governor.admitOrRefuse | scratch 预算 -1 字节 | AdmissionRefused 异常 + 结构化 reason（need/budget） | 同上 |
| R5 | AcquiredReduced | ModelSessionPool | 真实 VRAM 账本（FakeBackend 强制容量）下首选档放不进 | outcome=AcquiredReduced + grantedVramMb=首个可放档 + 账本余额 | 同上 |
| R6 | CpuFallback | 同上 | 账本满 | outcome=CpuFallback + session 为空 + 账本不变 | 同上 |

### 模拟层级（诚实标注，不充数）

| 层级 | 接缝 | 为什么本机不可真实触发 | 既有覆盖（消费，不重复） |
|---|---|---|---|
| 显存硬件级 OOM（CUDA/驱动层分配失败） | operators tile 引擎 batch→tile 逐级重试 | 本机无 GPU 与显存配额手段（无 CUDA quota/Job object 等价物） | test_model_failure_matrix.cpp 经 classifyInferenceError/provider 故障缝注入（ADR 0130 Consequences 明示该路径按属性可测）——本轨道消费其绿，不新增模拟用例充数 |
| 磁盘满 / fsync 失败（chunk 原子写家族） | DiskTileStore/ScratchLease 的 fsync/rename | 无故障注入点且本地模拟磁盘满是环境破坏性行为 | 原子性家族由 review P1 修复的真实 tmp→fsync→rename 路径测试覆盖（test_chunk_resume_11 / test_external_memory_10） |

（口径依据 DECISIONS.md D-6：预算记账失败路径即生产路径 = 真实。）
