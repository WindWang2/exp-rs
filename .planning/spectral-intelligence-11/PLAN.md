# PLAN — spectral-intelligence-11

Mission：局部/双窗 RX、稀疏解混、SID-SAM hybrid、端元与库分析、artifact 链路、Workbench 11、operators/capability、256–1024 band 规模。

## 原则

- 每包：现状证据 → 设计选择（≥2 候选写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract 同步 → commit。
- 算法真值独立：测试 oracle 用独立 closed-form / 高精度 reference，不复用被测实现。
- 新文件为主；共享注册文件 append-only。

## 阶段

| Phase | 内容 | 关键产出 |
|---|---|---|
| 0 | 审计（本文件 + BASELINE/PARALLEL_OWNERSHIP/ARCHITECTURE） | planning 落盘、架构图、worktree |
| 1 | 契约层：算法头文件 + JSON schema 草案 + capability descriptor 骨架 | 4 个新算法头/实现骨架 + capability JSON |
| 2 | A 局部/双窗 RX + C SID-SAM hybrid（核心数学 + known-answer tests） | `spectral_local_rx`、`spectral_hybrid_similarity` + tests |
| 3 | B 稀疏解混 + D 端元分析（ADMM/IRLS、聚类/去冗余/SAM 矩阵/传感器投影）+ tests | `spectral_sparse_unmixing`、`endmember_analysis` + tests |
| 4 | 算子 surface：4 个新 operator + 注册 + capability/task-family descriptors + diagnostics | `rs_operators_init` 追加、algorithm_meta JSON、agent capability |
| 5 | H 规模/故障硬化：256–1024 band、退化协方差、取消、内存上限、determinism | `test_spectral_scale` + PERFORMANCE.md |
| 6 | E artifact 链路 + F workbench 面板 + E2E known-answer | MNF→PPI→端元→sparse/unmix→match 全链路 test；GUI panel |
| 7 | 独立 adversarial review（subagent #2）+ P0/P1 修复 | REVIEW_LOG.md |
| 8 | 双验证、rebase、push、PR | PR # |

## Work packages → 设计要点

### A 局部/双窗 RX（`SpectralLocalRx`）
- 双窗 RX：outer window 估背景（排除 inner guard window），per-pixel 局部 mean/cov。
- 稳定协方差：shrinkage 正则（对角加载 α·tr(C)/B·I）替代裸 ridge；病态（rank 不足）→ 质量标志降级而非伪装。
- 窗口边界：镜像/截断策略；NoData：valid-pixel 计数下限，不足→NaN+质量位。
- streaming：行带 halo（outer radius），一次 covariance 累积 per 窗口行组；内存 O(rowband×bands²) 上界声明。
- 输出：RX 分数 + 质量/置信栅格（有效样本数、条件数带）。

### B 稀疏解混（`SpectralSparseUnmixing`）
- 目标：min ½||x−Ea||² + λ||a||₁ s.t. a≥0（可选 sum-to-one 罚，与 master FCLS 同型 ρ）。
- 求解器：ADMM（a-子问题非负 LS，z-软阈值）——确定性、无随机、有限停机容差。
- 病态端元检测：E^T E 特征值/条件数 + 端元对光谱角 < 阈值 → fail-closed 命名错误（同 FCLS 风格）。
- 上限：迭代/停机容差/最大端元数显式；收敛诊断输出（primal/dual residual）。

### C SID-SAM hybrid（`SpectralHybridSimilarity`）
- 定义在概率域：p=t/Σt、q=r/Σr；SID 为对称 KL；SAM 为角度。
- hybrid = SID·tan(θ)（经典 hybrid）与归一化加权 SID′·SAM′ 两种候选 → DECISIONS 裁决主形态，另一种保留为参数化。
- 波长对齐：wavelength reconciliation 通过 `SpectralWavelength::Grid` seam（缺波长 → fail-closed）。
- known-answer：常差谱、比例谱、正交谱、逐带构造的解析用例。

### D 端元与库分析（`EndmemberAnalysis`）
- 贪心去冗余：按 SAM 相似度层次聚类（average-link），每簇保留 PPI/PPI-count 最高代表；阈值参数化。
- 光谱角矩阵：n×n 对称矩阵 artifact（JSON + digest，provenance 继承输入）。
- 传感器投影：复用 `SpectralResampling`/`SpectralWavelength` seam；缺 FWHM → fail-closed。
- license/provenance：从输入 library/table 继承 license/digest 字段，丢则拒绝。

### E artifact 链路
- `exp-rs:spectral-table` / `exp-rs:mnf-transform` 既有 kind 不变；新增端元分析结果走 spectral-table kind（或其扩展字段，保持 placeholder 兼容）。
- fingerprint：输入 digest + 参数 canonical JSON → 稳定 fingerprint 写入 provenance。

### F Workbench 11
- 新 widget（非 D18、非 #1008 文件）：library artifact 载入、端元矩阵热图、ROI/点光谱、异常图层联动选择（信号/槽最小挂载）。
- offscreen 可测（QT_QPA_PLATFORM=offscreen 测试）。

### G operators/capability
- 4 个新算子：`rs:local_rx_anomaly`、`rs:sparse_unmixing`、`rs:spectral_similarity`、`rs:endmember_analysis`。
- algorithm_meta capability JSON + agent capability 更新 + diagnostics JSON 字段。

### H 规模
- 256/512/1024 band 合成数据 known-answer + 内存上界（声明式，不计时钟）+ determinism（同输入两次 bit-identical 或容差内）。

## 验证 gate

targeted ctest（-j1, offscreen）两遍；`git diff --check origin/master...HEAD`；冲突标记/secret 扫描；capability drift gate。
