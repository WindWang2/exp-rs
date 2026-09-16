# /goal — F03 · Hyperspectral & Spectral Intelligence 11.0

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 局部异常、稀疏解混、混合相似度与端元工作台升级
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/spectral-intelligence-11`
> **Worktree:** `../exp-rs-spectral-intelligence-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Prompt-generation snapshot（只用于启动审计，不是固定基线）

本 Prompt 生成时（2026-09-15）观测到：
- `origin/master` = `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`。
- open PR #991 `grok/unified-mission-workbench-d18`：MissionContext / IR2 dock / workflow mounting；head `8dbd6bde1aa8b0538c2e7f74bed3ba776db35a1f`。
- open PR #992 `grok/dataset-foundry-benchmark-d19`：Dataset Foundry / Benchmark；head `08264801a079efff683cd2446a0acee4c2153448`。
- 当时无独立 open issues；`ISSUES.md` 是旧 D3 backlog，其中多数条目已被后续 10.0 PR 修复，**禁止把它当实时 backlog 直接实施**。
- 最近 master 已合入 D14 geometric、D15 classification/change、D16 temporal、D17 workflow，以及 Data Fabric / Verification / Spectral / Execution / EO Model / Workbench 等 10.0 平台能力。

**启动时必须完全刷新这些事实。任何 SHA/PR 状态发生变化，以启动时 GitHub/origin 事实为准。**

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | 局部/双窗 RX | 稳定协方差、正则策略、窗口边界、NoData、streaming halo、异常置信/质量。 |
| B | 稀疏解混 | L1/非负/和为一约束的可解释 sparse unmixing，病态端元检测、收敛/上限。 |
| C | SID-SAM hybrid | 尺度/概率域定义、权重/阈值、wavelength reconciliation、known-answer。 |
| D | 端元与库分析 | 端元聚类/去冗余、光谱角矩阵、传感器投影、license/provenance 不丢失。 |
| E | artifact pipeline | MNF→PPI→artifact→match/unmix 的全链路与 cache/fingerprint，保持 placeholder 兼容。 |
| F | 光谱 Workbench 11 | library artifact、ROI/point spectra、端元矩阵、异常图层、联动选择；不改 D18 主工作台文件。 |
| G | operators/capability | 新增/升级算子 descriptor、task family、sidecars 和 diagnostics。 |
| H | 256–1024 band scale | 内存上限、数值稳定、退化协方差、取消、determinism/容差证据。 |

## GOAL Loop Oracle（未满足不得结束）

1. RX/sparse/SID-SAM 有独立 closed-form 或高精度 reference 真值
2. 不把病态矩阵/缺波长伪装成有效结果
3. artifact provenance/license/digest 在链路中保持
4. spectral targeted suites 连续两遍通过
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
6. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过（或同一明确、与本 diff 无关的 pre-existing/host limitation 被对照证明）。
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

（本文件为 GOAL 原文的要点存档；完整原文以任务指令为准。执行协议、ownership 规则、runbook、预算阶段表均按原文执行。）
