# R15-C PR Body（建 PR 时使用）

## 标题

`hardening(r15-c): 既有遥感 AI 模型推理、分块执行与 GPU/CPU 容错`

## 正文

Branch `hardening/r15-C-model-runtime`（worktree `exp-rs-r15-c-20261016`），base `origin/master` = `46791bc65c6f552ebbcf723bb91892736d56f319`，ahead 7 commits / behind 0（建 PR 时复测）。

### 修复索引（全部经 RED→GREEN×2 取证；详见 .planning/r15-c/）

| WP | 缺陷 | 严重度 | 证据 |
|---|---|---|---|
| C13 | DetectionPublishGuard 的 companion-park 崩溃后缀被**删除**而非收养 → 无 .shx 的不可打开矢量仍通过 provenance 校验 | **P1** | test_r15_C_publish_recovery（RED 2/3） |
| C13 | ProductPublishGuard 的 sidecar-park 崩溃后缀被销毁 → 已校验产品降级 MissingSidecar | P2 | 同上 |
| C05/C16 | 四个 provider 的 `*UnavailableReason()` 可恢复文案是死代码，用户只拿到一句通用错误 | **P1** | test_r15_C_provider_degradation（RED 3/3） |
| C11/C12 | ReduceConcurrency 接纳计划被当作"已接纳"，pipeline 仍以超预算容量运行 | **P1** | test_r15_C_chunk_admission census（RED 4/5） |
| C11 | VramLedger::tryReserve int 溢出（UB）→ 接纳权威被绕过；拒绝时留 0-MiB 幻影 holder；release 忽略数量 | P2 | 同上（ledger 3 用例） |
| C02 | 注册期交叉约束缺口（R14-B 显式遗留）：detection 固定帧契约、square-head 显式定义、heads dtype 词表 | P2 | test_r15_C_manifest_contracts（RED 3/6） |
| C14 | rs:detect conf/nms_iou 覆盖无范围校验 → conf=2.5 静默空产品、nms_iou=0 误导性报错 | P2 | test_model_tasks（RED 1/20） |
| C07 | 单输入引擎静默忽略 tiling.min_valid_coverage（文档化门） | P2 | test_r15_C_runtime_contracts（RED 1/2） |
| C05 | capabilities()/supportsMultiInput() 无消费方 → 多输入不匹配在 IO 之后才炸；python worker 协商值被丢弃 | P2 | test_r15_C_provider_degradation |
| C05 | 同一 dtype/shape 错误在 4 个 provider 上得到 3 种 ErrorCode | P2 | 同上 |
| C09 | weighted_vote agreement 带 NoData 像元被量化成越界值（死 isnan 分支） | P2 | test_model_ensemble（代码路径确证；GDAL 钳制掩盖可观测性，如实记录） |
| C04 | SICNU_MODEL_CUDA_DEVICES 无界（~2e9 向量 → OOM kill）；VRAM CSV 短于设备数静默丢数据 | P3 | 代码级（env 覆盖路径） |
| C05 | OpenVINO deviceName 破坏 payload 词表；TRT/OV artifactPath 编造身份；health/memoryEstimate 悲观默认 | P2/P3 | 代码级（compile-only 车道） |
| C12 | join 进度分母=sum(sources) → 2-input join 永远到不了 1.0；ChunkPipeline 双重运行重放旧错误 | P3 | 代码级（无生产调用方） |
| C11 | VRAM 预算拒绝映射 InvalidInputData（应为 ResourceBudgetExceeded） | P3 | 代码级 |
| C08 | dedup 键 int(lround) 在 >~2.1e7 px 回绕（UB） | P3 | test_r15_C_detection_decode（RED 1/5） |
| C16 | 能力门测试在完整部署主机上零断言假通过 | P3 | test_eo_platform_10 翻转 |

### 与 open/merged PR 的去重

- **#1505（merged）**：未重写其 gpu_plane/python-pool/SSRF 修复；仅回归验证（test_gpu_plane 35、test_model_failure_matrix 2158 全绿）。
- **#1508（merged, R14-B）**：其 ORACLES/FINAL_REPORT 所列修复未重做；本轨 C02 正是其显式遗留项（"registration-time square-head 禁入缺口属 framework owner"）。
- **#1509（merged，本 BASE 已含）**：jsoncpp typed safety 未触碰。
- **#1514（open, r15-A）**：文件零重叠。
- **#1513（open draft, r15-B）**：唯一共享文件 `tests/CMakeLists.txt`——本轨注册块插入模型运行时测试区（~3042 行），#1513 追加块在文件尾部（9818+），git 可干净合并；未 cherry-pick。

### 测试

- 25 个测试目标 GREEN ×2（新增 6 个 `test_r15_C_*` + 修改 3 个既有文件 + 回归 16 个）；`git diff --check` 干净。
- RED：stash 全部 src 修复后 8/9 新目标按预期失败（C09 被 GDAL 钳制掩盖，已如实记录）。
- 性能：test_model_runtime_bench 同宿主对比持平/略优（PERFORMANCE.md）。
- **NOT VERIFIED**：CUDA/TRT/OpenVINO/ORT 车道（无依赖）；Linux；test_device_planner 在本宿主挂起（master 同样挂起，预先存在的环境问题）。

### 独立 Review

两轮（reviewer 未参与本轨生产代码编写）：REVIEW_ROUND1.md / REVIEW_ROUND2.md。

### 受保护文件

未修改：`src/runtime/gpu/gpu_plane.cpp`、`src/python/isolated/**`、`src/agent/**`、`src/workflow/**`、`src/processing/algorithms/**`、`src/geospatial/**`、`src/operators/rs/**`、`.github/workflows/**`。
