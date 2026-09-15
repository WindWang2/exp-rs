# GOAL — F15 · Large-Scale Mosaic, Fusion & Quality Composite 11.0

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 接缝线、匀色、质量镶嵌、多尺度融合与大图流式生产
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/mosaic-fusion-11`
> **Worktree:** `../exp-rs-mosaic-fusion-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Mission definition

最终产品不是"写了一批代码"，而是：**接缝线、匀色、质量镶嵌、多尺度融合与大图流式生产**，
并且其 authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface
（适用时）相互一致。先证明已有能力和真实缺口，再实现；对所有科学公式/坐标/单位/时间/NoData 语义采用
独立真值，不允许测试复用被测实现来制造绿灯。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | scene graph/grid plan | extent/resolution/CRS/priority/NoData/overlap inventory，跨 CRS 显式预处理。 |
| B | radiometric balancing | overlap statistics、gain/bias/robust normalization、reference selection、异常场景拒绝。 |
| C | seamline | cost surface、graph/path、cloud/edge/gradient penalties、deterministic tie-break。 |
| D | blending | feather/multiband pyramid，halo 与 seam exactness，大图有界内存。 |
| E | quality mosaic | cloud/quality/time/view angle 等 score，per-pixel provenance/scene index。 |
| F | fusion quality | pan-sharpen GS/Brovey/IHS/HPF 回归 + Wald quality report，防 spectral distortion。 |
| G | atomic streaming output | tile/overview/COG-friendly publication，通过 public runtime seam，不复制 execution engine。 |
| H | mosaic corpus | overlap radiometry/cloud/seam synthetic truth、100k logical tile scale、cancel。 |

每个 package 都必须包含：现状证据 → 设计选择（至少两个候选时写 DECISIONS）→ 最小 vertical slice →
known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。

## GOAL Loop Oracle（未满足不得结束）

1. seamline/blend 不产生 NoData 裂缝或重复边界
2. quality composite 每像元 source 可追溯
3. 大图逻辑测试内存随 tile 而非全图增长
4. mosaic/fusion suites 两次通过
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
6. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过（或同一明确、与本 diff 无关的
   pre-existing/host limitation 被对照证明）。
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

`.goal-loop-ledger.md` 每轮格式：

```text
Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步
```

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题，不弹选项；有歧义时选择最保守、最兼容、最少重复实现的方案，并写入 DECISIONS.md。
- `master` 永远只读；所有编辑只发生在本 track 独立 worktree/branch。
- 不 merge 其他开发分支，不改别人的 worktree，不 force-push。
- 不等待、不重跑、不引用线上 CI 作为完成证据。
- CMake 优先使用 `CMakePresets.json`/`build-dev`。硬资源：`CMAKE_BUILD_PARALLEL_LEVEL=2`、
  `CTEST_PARALLEL_LEVEL=1`；Ninja/CMake build 只能 `-j2` 或 `-j1`；禁止 `-j$(nproc)`。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；先 targeted suite，再按必要性扩大；测试 `-j1`。
- 编译期间每 60 秒记录一次 CPU/RSS/负载。
- 不新增重量级依赖。
- 所有输出/缓存/sidecar/数据库写入必须考虑 atomicity、cancel、失败清理、Unicode path、read-only source、
  NoData/CRS/provenance。
- 不把 wall-clock benchmark 当 correctness gate。

## Model / subagent policy

**Subagents：最多 2 个，硬上限。** #1 只读做 Phase 0 基线/科学审计，#2 只读做最终对抗 review；
subagent 不得再 spawn subagent，主 agent 完成全部写代码与裁决。

## Skills（存在才加载）

`.agents/skills/{codebase-design,code-review,diagnosing-bugs,domain-modeling,ask-matt,implement-spec|implement}/SKILL.md`；
冲突时 `resolving-merge-conflicts`。goal-loop 协议内嵌执行（`.goal-loop-ledger.md` 逐轮记账）。

## Token budget / execution phases（总 500M）

Phase 0 审计/规划 30M · Phase 1 契约/模型 80M · Phase 2 核心算法一 90M · Phase 3 核心算法二 75M ·
Phase 4 surface 65M · Phase 5 硬化 55M · Phase 6 E2E/drift 45M · Phase 7 review 35M · Phase 8 终验/PR 25M。
每 Phase 超 1.5× 预算时在 EVIDENCE 记录原因并继续，不提问。

## Required planning/evidence artifacts

CURRENT_ARCHITECTURE.md / CAPABILITY_MATRIX.md / PARALLEL_OWNERSHIP.md / TEST_MATRIX.md / PERFORMANCE.md /
REVIEW_LOG.md / BASELINE.md / DECISIONS.md / EVIDENCE.md / PLAN.md / PR_BODY.md / .goal-loop-ledger.md。

## Worktree / commit / rebase / PR runbook

worktree `../exp-rs-mosaic-fusion-11` off `origin/master`；每 Phase ≥1 原子 commit；
每 Phase commit 后 `git fetch origin && git rebase origin/master`；共享注册文件独立 integration commit、
append-only；完成实现后主 agent 全 diff review → 独立 reviewer → P0/P1 修复重跑 gate →
`git diff --check origin/master...HEAD` + 冲突标记/secret 扫描 + targeted tests 连续两次 →
push + `gh pr create --base master`（不 merge，不等 checks）。

## Autonomy defaults

1. 格式/权威来源：优先 master 已有 registry/schema/domain authority；禁止第二真值。
2. 失败项：先根因分析；环境缺失才标 not-executed；单测真实失败不得跳过或删 test 换绿。
3. 命名/编号：沿用现有 namespace/operator/error/ADR 规则；冲突时最小 additive 并记录。
4. 资源/超时：build `-j2`→压力高降 `-j1`；test `-j1`；scale 测试 env/label opt-in。
5. 对外动作：允许 git fetch、读 GitHub、push 自己分支、创建自己 PR；禁止 merge/close 他人 PR/issue。
6. 范围外发现：EVIDENCE `OUT_OF_SCOPE`；P0 写 PR_BODY 顶部。
7. 新依赖：默认不用。
8. 并发冲突：rebase + 重新审计；不得复制实现避冲突。
9. 文档与旧 backlog 只做线索，缺口必须对当前 code 重新验证。

---

*注：原文为 2026-09-15 生成的完整 `/goal` prompt；本文件为忠实存档（格式微调，语义未改）。
启动快照（SHA/PR 状态）与启动时实测差异见 BASELINE.md。*
