# PERFORMANCE — multimodal-registration-11

资源模型（GOAL 硬约束）：build `CMAKE_BUILD_PARALLEL_LEVEL=2`（load > 1.5×16=24 才降 `-j1`），
test `CTEST_PARALLEL_LEVEL=1`，`QT_QPA_PLATFORM=offscreen`。启动时主机 16 核 / 64 GiB，
load 3.23；构建期间 load 13–17（低于 24 阈值，维持 -j2），RSS 峰值 ~13.5 GiB / 64 GiB（21%）。
60s 采样以 `uptime`+`free -m` 轮询记录于 shell 历史（本轮未做定时器自动化，见 EVIDENCE 说明）。

## 各算法复杂度与上限（逻辑规模，非 wall-clock）

| 组件 | 复杂度 | 上限机制 |
|---|---|---|
| fft2d（每窗口相位相关） | O(N² log N)，N=补零窗边（≤256） | windowSize clamp 16..256 |
| MultimodalMatcher | 每级 O(格点数)×相位相关；MI/NCC 精级 O(格点)×(2r+1)²×窗采样（stride 2 当窗>48） | maxMatches cap（默认 4000，按 score 保留 top-K）；金字塔层级以 min(dim)≥32 封顶；estimateScratchMiB > maxWorkingMiB（默认 512 MiB）→ cap_exhausted refusal |
| ModelSelector | 每候选 k 次拟合 + O(k·N) 评估；梯形 7 候选 | folds clamp ≥2；点数硬门限 |
| RpcBiasModel | O(N) 中位数 + O(N) 3x3 正规方程 ×k folds | minSamples 门限 |
| StackRegistrator | 稠密正规方程 (2·maxScenes)² 消元 | maxScenes=256（≈2 MiB double）→ cap_exhausted |
| RegistrationQuality | O(points²) 支撑计数（支持半径邻域）+ O(N log N) 分位数 | points ≤ matcher cap |
| rs:register_images | maxDim clamp 64..4096（三 Float32 buffer + FFT scratch） | 内存上界 ~3·maxDim²·4B + 4 MiB |

## 已测内存契约

- estimateScratchMiB closed-form 已知值测试（512²/窗64 → 4.19 MiB）。
- 生成物 sidecar 均为 QSaveFile 原子写（temp+rename），失败不留半文件。
