# GOAL — F08 · Scientific Workflow Compiler & Grounding 11.0（原文逐字存档）

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 更强事实推理、时空契约、执行 provenance 与真实数据 grounding
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/scientific-workflow-compiler-11`
> **Worktree:** `../exp-rs-scientific-workflow-compiler-11`
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

Compiler 10.0 已有 typed WorkflowIR、17 类静态检查、repair/refusal、session checkpoint；明确后续是 execution provenance 消费、时间日历事实、live GDAL grounding E2E。

本段只是生成 Prompt 时的方向判断。执行者必须以 Phase 0 重新读取的 master/PR/issues/reviews/code 为准；如果现状已经覆盖某个 package，不重复造轮子，而是在本领域内向下一个可验证缺口深化，并把 rescope 写入 `DECISIONS.md`。

## Phase 0 必做：在创建本 track worktree 前读取最新 master / PR / issue / review / branch

（……按 GOAL runbook 执行；原始审计输出存于 BASELINE.md……）

## Ownership / parallelism

**Primary write scope（启动审计后可收窄，不可无理由扩大）：**
- `src/agent/harness/**`
- `src/agent/*ground*`
- `src/agent/*workflow*`
- `pi/**harness**`
- `tests/*harness*`
- `docs/agents/**`

**默认 read-only / 避免并发冲突：**
- src/workflow/** while #991 open
- D19 dataset/benchmark files

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题，不弹选项；有歧义时选择最保守、最兼容、最少重复实现的方案，并写入 `DECISIONS.md`。
- `master` 永远只读；所有编辑只发生在本 track 独立 worktree/branch。
- 不 merge 其他开发分支，不改别人的 worktree，不 force-push。
- 不等待、不重跑、不引用线上 CI 作为完成证据。
- CMake 优先使用 `CMakePresets.json`/`build-dev`。硬资源：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；Ninja/CMake build 只能 `-j2` 或 `-j1`。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；先 targeted suite，再按必要性扩大；测试 `-j1`。
- 编译期间每 60 秒记录一次 CPU/RSS/负载。
- 不新增重量级依赖。
- 所有输出/缓存/sidecar/数据库写入必须考虑 atomicity、cancel、失败清理、Unicode path、read-only source、NoData/CRS/provenance。
- 不把 wall-clock benchmark 当 correctness gate。

### Model / subagent policy

Subagents：最多 2 个，硬上限。#1 只读 Phase 0 审计，#2 只读最终对抗 review；主 agent 完成全部写代码与裁决。

## Mission definition

最终产品：**更强事实推理、时空契约、执行 provenance 与真实数据 grounding**，authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface 相互一致。先证明已有能力和真实缺口，再实现；对所有科学公式/坐标/单位/时间/NoData 语义采用独立真值。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | fact model 2.0 | 时间 cadence/regularity、spatial resolution/extent、quality mask、product generation、model task、resource facts，保持来源等级。 |
| B | live grounding | GDAL/sidecar/product registry/model manifest 的 bounded factual probes，cache/invalidations/timeout。 |
| C | analysis 2.0 | 跨时间/波段/网格/数值域/模型/资源/输出 identity 的静态检查，UNKNOWN 不伪 PASS。 |
| D | repair planning | 区分 shape-preserving 自动 repair 与 science-changing prepared decision；风险/证据/成本排序。 |
| E | execution provenance projection | 编译 fingerprint、facts、repairs、refusals 以稳定 metadata 交给已有执行入口；不修改 #991 IR2 文件。 |
| F | explain/diagnose | 从 compile→run→failure 回溯哪条事实/契约导致决定，中文可读且 bounded。 |
| G | eval corpus | 真实小 GDAL fixtures、时序/光谱/SAR/model mixed workflow，anti-hallucination/refusal cases。 |
| H | Pi/ZCode bridge robustness | knowledge budget、desync/timeout/abort、session resume、schema drift。 |

每个 package 都必须包含：现状证据 → 设计选择（至少两个候选时写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。

## Token budget / execution phases

总预算 **500,000,000 tokens**。Phase 0-8: 30M/80M/90M/75M/65M/55M/45M/35M/25M。

## Autonomy defaults

1. 优先 master 已有 authority；禁止第二份真值。2. 失败先根因。3. 沿用现有命名。4. build -j2→-j1；test -j1。5. 允许 fetch/读 GitHub/push 自己分支/建自己 PR；禁止 merge。6. 范围外发现写 EVIDENCE OUT_OF_SCOPE。7. 默认不加依赖。8. 冲突优先 rebase。9. 旧 backlog 仅做线索。

## GOAL Loop Oracle（未满足不得结束）

1. UNKNOWN facts 永不被输出成 verified/pass
2. 科学改变类 repair 不静默自动插入
3. 同输入 facts→同 normalize/fingerprint/plan
4. harness suites+eval corpus 连续两次通过
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；生成物 drift gate clean
6. Phase 8 关键 targeted validation 原样连续运行两遍，两次通过
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge

## Worktree / commit / rebase / PR runbook

（按 GOAL 原文执行：worktree→planning 工件→每 Phase 原子 commit→每 Phase 后 rebase→共享文件推迟到 integration commit→review→双验证→push→PR→不 merge。）

## Final review instructions

Review 从 `origin/master...HEAD` 完整 diff 开始；覆盖 architecture/authority/duplication、science/CRS/units/time/NoData/provenance、concurrency/cancel/lifetime/atomicity/resource bounds、API/schema/platform 兼容、test oracle independence、security/secret/path/offline、docs/help/capability/contract drift。修完后再 rebase、双跑 targeted gate、再 PR。最终只报告实际完成与证据。
