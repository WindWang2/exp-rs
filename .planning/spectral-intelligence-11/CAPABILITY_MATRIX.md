# CAPABILITY_MATRIX — spectral-intelligence-11

Before/after（○ = master 既有；✓ = 本 track 交付；✗ = non-goal；◐ = 降级/受限交付）。

| Capability | Before (master a5b11b7f) | After (this track) | Notes |
|---|---|---|---|
| 全局 RX 异常 | ○ `rs:rx_anomaly`（流式、NoData） | ○ 不变 | 不动既有算子 |
| 局部/双窗 RX | ✗ | ✓ `SpectralLocalRx` + `rs:local_rx_anomaly` | Full/Diagonal 协方差、guard 窗、scaled loading、质量面（backgroundSamples/scored）、tile 无关确定性 |
| OLS 解混 | ○ | ○ | — |
| FCLS 解混 | ○ | ○ | #1008 另有重写，read-only |
| 稀疏解混（ℓ1+非负[+Σ=1 罚]） | ✗ | ✓ `SpectralSparseUnmixing` + `rs:sparse_unmixing` | FISTA+精确 prox（非负∩软阈值闭式）、病态端元 fail-closed、收敛诊断 |
| SAM / SID | ○ | ○ | master 为单一真值 |
| SID-SAM hybrid | ✗ | ✓ `SpectralHybridSimilarity` + `rs:spectral_similarity` | ProductNormalized（默认，[0,1]）+ ClassicTan；波长 grid 守卫 |
| PPI 端元提取 | ○ | ○ | — |
| 端元聚类/去冗余 | ✗ | ✓ `EndmemberAnalysis::reduceEndmembers` | average-link SAM 阈值聚类 + PPI 代表，确定性 |
| 端元光谱角矩阵 | ✗（仅 pairwise 隐式） | ✓ `EndmemberAnalysis::angleMatrix` | 对称、对角 0，digest 继承 |
| 端元→传感器投影 | ◐（reference seam 内部有重采样） | ✓ `EndmemberAnalysis::projectToSensor`（表/库端元 → 带 FWHM 的 sensor grid） | 缺 FWHM/波长 → typed refusal |
| 端元分析 artifact | ✗ | ✓ spectral-table kind 扩展字段（matrix/clusters/provenance 继承） | 不建第二工件权威 |
| MNF→PPI→artifact→unmix 链路 | ○ | ○ + ✓ 稀疏/hybrid 消费同一 seam | placeholder 兼容保持 |
| Workbench GUI | ○ Spectral Curve dock | ✓ 新 Spectral Workbench 11 dock | library/table 工件载入、端元矩阵、ROI/点谱、异常联动；不碰 D18/#1008 文件 |
| 256–1024 band 规模 | ◐ MNF kMaxBands=1024、10.0 256-band case | ✓ 256/512/1024 known-answer + determinism + 内存上界证据 | Diagonal RX、FISTA 内存 O(B·E)、时钟不作 gate |
| capability/agent surface | ○ 32 算子 catalog | ✓ +4 算子 descriptor + capability JSON + 生成物重导出 | drift gate 全绿 |

## Not supported / degraded（诚实边界）
- Full 协方差局部 RX 在 B≥~512 且窗口大时计算量 O(w²B²)/px —— 提供 Diagonal 模式并在 executionEstimate 声明；不在本 track 做 FFT/子空间加速。
- 稀疏解混 sum-to-one 为罚（与 master FCLS 同约定），非硬 KKT；QA 报 |Σa−1|。
- ClassicTan 正交谱 → +inf（形式性质，文档化）。
- 比较全部本地证据；不引用在线 CI。
