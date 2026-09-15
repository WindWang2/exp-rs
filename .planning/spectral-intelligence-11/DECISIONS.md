# DECISIONS — spectral-intelligence-11

## D1 — 与 open PR #1008 的关系（Phase 0）
- 候选：(a) 等 #1008 合并后基于其重审计；(b) 完全避开其 changed files，新文件 + append-only 共享文件；(c) 复制其 FCLS/VCA 到本 track。
- **裁决：(b)。** #1008 CONFLICTING 未合并，等待不可控；复制违反“禁止同功能换名字”。本 track 的 sparse unmixing/hybrid similarity 与其 FCLS/SAM-SID 是不同能力；共享注册文件只追加。

## D2 — 双窗 RX 协方差正则
- 候选：(a) 裸对角 ridge（master 全局 RX 的 1e-9 常数）；(b) scaled diagonal loading α·(tr Σ/B)·I；(c) Ledoit-Wolf shrinkage。
- **裁决：(b)** α 参数化（默认 1e-3，schema 可调），窗口样本少时稳健且闭式可预测；(c) 依赖数据估计强度、known-answer 更难独立复算；(a) 在局部小样本下不足。质量输出带有效样本数与 loading 比。

## D3 — sparse unmixing 求解器
- 候选：(a) ADMM（非负 LS + 软阈值）；(b) IRLS-ℓ1；(c) LASSO+投影。
- **裁决：(a) ADMM**：split 天然容纳 a≥0 与 ℓ1，迭代确定、residual 可诊断、停机判据显式（primal/dual ε）；sum-to-one 用与 master FCLS 同型罚项 ρ·11ᵀ（一致性优先，不建第二约束体系）。病态端元集（条件数/共线角）fail-closed。

## D4 — SID-SAM hybrid 主形态
- 候选：(a) hybrid = SID·tan(θ)（Chang 经典）；(b) 归一化乘积 SID′·SAM′∈[0,1]；(c) 加权和。
- **裁决：提供 (a) 与 (b) 两形态，枚举参数选择；默认 (b)**（有界、可解释、跨谱库可比；(a) 无界且 θ→π/2 爆炸）。(c) 无独立科学动机，不做。known-answer 分别给两类真值。

## D5 — 端元去冗余阈值与代表选择
- 候选：(a) 固定角度阈值 + PPI 计数最高为代表；(b) 谱距离 k-means。
- **裁决：(a)** average-link 层次合并（阈值默认 2°，schema 可调）；代表=簇内 PPI 计数最高（平局取 index 最小，确定性）。k-means 引入随机/迭代语义且与 PPI 输入语义重叠。

## D6 — 端元分析 artifact 载体
- 候选：(a) 新 artifact kind；(b) 复用 `exp-rs:spectral-table` + 扩展字段。
- **裁决：(b)**：table 已有 provenance/license/digest 权威与校验规则（10.0），避免第二真值；矩阵/聚类信息放扩展对象字段，`validate` 规则同步扩展。保持 placeholder 兼容（路径占位语法不变）。

## D7 — Workbench 11 GUI 挂载点
- 候选：(a) 修改 D18 mission workbench 主文件加 tab；(b) 独立 dock widget 新文件 + 主窗口最小挂载。
- **裁决：(b)**。GOAL 禁改 D18 主工作台文件；挂载仅一行级 append。

## D8 — 新算子命名
- 沿用 `rs:` 前缀与 snake_case：`rs:local_rx_anomaly`、`rs:sparse_unmixing`、`rs:spectral_similarity`、`rs:endmember_analysis`。与 `rs:rx_anomaly`/`rs:spectral_unmixing` 的关系：互补（global→local、FCLS→sparse、独立相似度算子），不动旧算子语义。

## D9 — 规模 gate 形态
- 时钟 benchmark 不作 correctness gate；用 bounded logical scale（512/1024 band 合成）、内存上界断言（容器 capacity 上限/批处理上限）、determinism 断言。可选 env（`EXP_SPECTRAL_SCALE_STRESS=1`）才跑大 case。

## D10 — 测试文件与 target 命名
- 每能力一文件：`test_spectral_local_rx`、`test_spectral_sparse_unmixing`、`test_spectral_hybrid_similarity`、`test_endmember_analysis`、`test_spectral_scale`（如与现状冲突以现状 registry 为准）。
