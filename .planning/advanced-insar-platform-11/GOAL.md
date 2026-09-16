# GOAL — F01 · Advanced InSAR Scientific Platform 11.0（原文存档）

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 严谨轨道/DEM去地形相位、解缠provider与多时相干涉网络
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/advanced-insar-platform-11`
> **Worktree:** `../exp-rs-advanced-insar-platform-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Prompt-generation snapshot（只用于启动审计，不是固定基线）

本 Prompt 生成时（2026-09-15）观测到：
- `origin/master` = `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`。
- open PR #991 `grok/unified-mission-workbench-d18`：MissionContext / IR2 dock / workflow mounting；head `8dbd6bde1aa8b0538c2e7f74bed3ba776db35a1f`。
- open PR #992 `grok/dataset-foundry-benchmark-d19`：Dataset Foundry / Benchmark；head `08264801a079efff683cd2446a0acee4c2153448`。
- 当时无独立 open issues；`ISSUES.md` 是旧 D3 backlog，其中多数条目已被后续 10.0 PR 修复，**禁止把它当实时 backlog 直接实施**。
- 最近 master 已合入 D14 geometric、D15 classification/change、D16 temporal、D17 workflow，以及 Data Fabric / Verification / Spectral / Execution / EO Model / Workbench 等 10.0 平台能力。

**启动时必须完全刷新这些事实。任何 SHA/PR 状态发生变化，以启动时 GitHub/origin 事实为准。**

## Why this track now

SAR 10.0 已交付复数 SLC、PolSAR、基础 InSAR 和日历化时序，但 PR 明确留下 baseline-aware interferometry、DEM/orbit topographic phase、外部 unwrap provider 与更完整的多时相干涉能力。

本段只是生成 Prompt 时的方向判断。执行者必须以 Phase 0 重新读取的 master/PR/issues/reviews/code 为准；如果现状已经覆盖某个 package，不重复造轮子，而是在本领域内向下一个可验证缺口深化，并把 rescope 写入 `DECISIONS.md`。

## Phase 0 必做：在创建本 track worktree 前读取最新 master / PR / issue / review / branch

（原文照录执行命令清单：git fetch/rev-parse/log/branch -r、gh pr list、gh issue list、gh pr view/diff、ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md 只读核验，建立 PARALLEL_OWNERSHIP.md。规则五条照原文执行，见本 track BASELINE.md 与 PARALLEL_OWNERSHIP.md 的落实。）

## Ownership / parallelism

**Primary write scope（启动审计后可收窄，不可无理由扩大）：**
- `src/processing/algorithms/sar/**`
- `src/operators/rs/*sar*`
- `data/agent/capabilities/sar.json`
- `docs/processing/sar-domain.md`
- `tests/*sar*`

**默认 read-only / 避免并发冲突：** D18/D19 owned paths；non-SAR model/workflow internals。

## Operating envelope（不可协商）

全自动不提问；master 只读；不 merge 他人分支；不等待线上 CI；`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、Ninja `-j2`/`-j1`；测试 `QT_QPA_PLATFORM=offscreen`、`-j1`；编译期间每 60s 记录资源；不新增重量级依赖；所有输出考虑 atomicity/cancel/Unicode/read-only source/NoData/CRS/provenance；不把 wall-clock 当 correctness gate。

### Model / subagent policy

Subagents 最多 2 个，全部只读：#1 Phase 0 基线/科学审计，#2 最终对抗 review；主 agent 完成全部写代码与裁决。

## Mission definition

最终产品：**严谨轨道/DEM去地形相位、解缠provider与多时相干涉网络**，authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface 相互一致。先证明已有能力和真实缺口，再实现；科学公式/坐标/单位/时间/NoData 语义采用独立真值。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | 轨道/基线真值 | interferometricBaseline、轨道状态插值、LOS/垂直基线、波长与几何元数据统一为版本化输入真值 |
| B | DEM/orbit topographic phase | 真正的几何去地形相位链，明确参考椭球/DEM/轨道/相位符号；无必要元数据必须拒绝 |
| C | 共注册与相干质量 | 多尺度复数共注册、局部偏移场、coherence/phase quality、mask、边界/NoData 语义 |
| D | unwrap provider framework | 内置 reference unwrap 保留；外部 provider adapter seam（如 SNAPHU 可选），进程/文件/错误/取消/清理安全 |
| E | 多时相 pair network | 时间-空间基线约束的 pair graph、连通性检查、reference、闭合相位 QA |
| F | 基础时序反演 | 非 PSI 完整实现前提下的小基线线性形变/残差估计、缺测与权重处理、质量产品 |
| G | 算子/知识/文档 | rs:sar_* 操作面、capability sidecar、failure codes、provenance 和中文领域文档 |
| H | known-answer + scale | 解析相位坡面、DEM 几何、闭合环、解缠、位移单位、取消和内存上限 |

## Token budget / execution phases

总预算 **500,000,000 tokens**。Phase 0–8 分配 30M/80M/90M/75M/65M/55M/45M/35M/25M；超 1.5× 记录原因并继续。

## Autonomy defaults

（九条照原文执行：master 既有 authority 优先；失败项先根因；最小 additive 命名；build -j2→-j1；对外动作允许 fetch/读 GitHub/push 自己分支/建自己 PR；范围外发现写 OUT_OF_SCOPE；新依赖默认不用；冲突 rebase+重审计；ISSUES.md/CHANGELOG 只做线索。）

## GOAL Loop Oracle（未满足不得结束）

1. 解析 synthetic interferogram 在已知 DEM/轨道下去地形相位后残差满足预设容差
2. pair graph 断连/缺元数据/波长不一致时 fail-closed
3. unwrap provider 缺失/崩溃/超时不遗留半成品
4. SAR targeted tests 连续两次通过且 review P0/P1=0
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；生成物 drift gate clean
6. Phase 8 关键 targeted validation 原样连续运行两遍，两次都通过
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge

`.goal-loop-ledger.md` 每轮格式：`Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步`

## Required planning/evidence artifacts

CURRENT_ARCHITECTURE.md、CAPABILITY_MATRIX.md、PARALLEL_OWNERSHIP.md、TEST_MATRIX.md、PERFORMANCE.md、REVIEW_LOG.md、必要 MIGRATION/SCHEMA/FAILURE_MATRIX。

## Worktree / commit / rebase / PR runbook

（十步照原文执行：planning 文件落盘、`.planning` 白名单验证、每 Phase 原子 commit、rebase origin/master、共享注册文件独立 integration commit、主 agent 全 diff review + 独立 reviewer、双验证、push、gh pr create、PR_BODY 完整披露。）

## Final review instructions

Review 从 `origin/master...HEAD` 完整 diff 开始：architecture/authority/duplication；science/math/CRS/units/time/NoData/provenance；concurrency/cancel/lifetime/atomicity/resource bounds；API/schema/backward compat/portability；test oracle independence、negative/scale credibility；security/secret/path/remote/offline；UI lifecycle（若有 UI）；docs/help/capability/contract drift。修完 rebase + 重跑 targeted gate 两遍再 PR。
