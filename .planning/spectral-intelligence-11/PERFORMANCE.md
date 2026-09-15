# PERFORMANCE — spectral-intelligence-11

资源模型与逻辑规模（时钟不作 correctness gate；单位为操作数/内存上界/确定性断言）。

## 资源上限（GOAL 硬约束）

- 构建 `CMAKE_BUILD_PARALLEL_LEVEL=2`、Ninja `-j2`；测试 `CTEST_PARALLEL_LEVEL=1`、`-j1`。
- 宿主 Windows/Git Bash 无 load average → 记录为 not-executed；以 tasklist RSS 抽查代替。
- 单个 scale 用例可通过 ctest 选择性运行（`ctest -R test_spectral_scale::`）；日常 gate 为 bounded logical scale（本文所列全部为 bounded）。

## 各内核成本模型

| 内核 | 时间（每像素/每对） | 空间 | 上界/防护 |
|---|---|---|---|
| Local RX Full | O(w²·B²)（w=外窗像素数） | O(w·B) gather + O(B²) 瞬态 | B > 8192 → typed refusal（防御性，防 B² doubles 爆内存） |
| Local RX Diagonal | O(w²·B) | O(w·B) + O(B) | 1024 band 实测通过（test_spectral_scale::Diagonal） |
| Sparse unmixing build | O(n²·B) Gram + 64 次幂迭代 O(64·n²) | O(n²) Gram + O(n·B) E | n > 2048 atoms → typed refusal（32 MB Gram 上限） |
| Sparse per-pixel FISTA | O(iter·n²)；收敛后停止 | O(n) 向量若干 | maxIterations 显式；未收敛 honest flag（converged/iterations per-pixel） |
| Hybrid similarity | O(B) per pair | O(B) | — |
| Angle matrix | O(n²·B) | O(n²) doubles | reduce 上限 512；算子 matrix embed 上限 64 行 |
| Reduce (average-link) | O(n³) 最坏（n≤512 → ≤1.3e8） | O(n²) | n > 512 → typed refusal |
| Sensor projection | O(n·B_src·B_dst) | O(n·B_dst) | requireFull 覆盖率 flag/refusal |
| 算子 streaming | O(tile+halo) 常驻 | halo=外窗半径 | tile 256²；GdalStreamingOutput 失败/取消 abandon() |

## 规模证据（test_spectral_scale，逻辑断言）

- 1024 band identity 字典：FISTA 解 == max(x−λ,0) 逐坐标（margin 1e-4），converged=1，两次运行 **bit-identical**（memcmp）。
- 1024 band × 256 atoms 超完备字典：buildDictionary 成功（0.5 MiB Gram），lipschitz>0，单像素解非负。
- 1024 band Diagonal local RX（24×24）：与测试内独立参考（方差式）一致（margin 1e-3），重复运行 bit-identical。
- Full covariance 在 B=8193 → typed refusal（消息含 "8192"）；Diagonal 同输入仍可运行。
- Hybrid similarity / angle matrix / reduce 在 1024 band 行：恒等谱 similarity=1；矩阵对称、对角 0；reduce 两次运行同代表。

## 算子诊断（无伪装失败）

- `rs:local_rx_anomaly`：qualityOut 样本数面 + scoredPixels/unscoredPixels 计数；NaN=unscored（不伪造 −9999）。
- `rs:sparse_unmixing`：convergedFraction、meanIterations、meanAbundanceSumDeviation（Σ=1 为罚，如实报告偏差）。
- `rs:spectral_similarity`：meanScore、labelledPixels；未定义谱 → -9999（label）。
- `rs:endmember_analysis`：input/output digest、license 继承、clusterOf/representativeOf 全量映射。
- 取消：全部 tile 循环 `context.throwIfCancelled()`；local RX 失败/取消路径 `GdalStreamingOutput::abandon()` 删除半成品。

## 构建资源观测

（构建完成后记录：命令、CPU/RSS 抽查时间点。）
