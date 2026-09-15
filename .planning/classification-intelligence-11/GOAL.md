# GOAL — F12 · Classification & Object Intelligence 11.0（原文存档）

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 概率分类、特征管线、空间验证、不确定性和对象级后处理升级
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/classification-intelligence-11`
> **Worktree:** `../exp-rs-classification-intelligence-11`
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

D15 已有 classical kernels、GLCM、spatial split、CVA、accuracy studio；下一步可在不抢 D19 Dataset Foundry 的前提下深化模型概率/校准、特征组合、对象级推理、空间 CV 与不确定性。

## Ownership / parallelism

**Primary write scope（启动审计后可收窄，不可无理由扩大）：**
- `src/analysis/classification/**`
- `src/processing/algorithms/*classif*`
- `src/app/workbench/classification_studio_widget.*`
- `src/operators/rs/*classif*`
- `tests/*classif*`
- `docs/processing/classification*`

**默认 read-only / 避免并发冲突：**
- D19 dataset/feature/benchmark code
- D18 mission files

并行开发原则：本 track 的业务主体必须落在上述 primary scope；对于共享 integration files 只做最小 append-only 接线。若发现另一个 open PR 已在相同业务主体开发，优先消费它在 master 上已有的稳定 seam、改成本 track 的互补能力；禁止"同功能换名字再写一遍"。

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题，不弹选项；有歧义时选择最保守、最兼容、最少重复实现的方案，并写入 `DECISIONS.md`。
- `master` 永远只读；所有编辑只发生在本 track 独立 worktree/branch。
- 不 merge 其他开发分支，不改别人的 worktree，不 force-push。
- 不等待、不重跑、不引用线上 CI 作为完成证据；PR 创建后即使自动触发 Actions 也不等待。所有 capability claim 只允许来自本地可复现 evidence。
- CMake 优先使用 `CMakePresets.json`/`build-dev`。硬资源：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；Ninja/CMake build 只能 `-j2` 或 `-j1`；禁止 `-j$(nproc)`。RSS > 70% 或系统负载过高时降到 `-j1`。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；先 targeted suite，再按必要性扩大；测试 `-j1`。
- 编译期间每 60 秒记录一次 CPU/RSS/负载；无法测量时在 EVIDENCE 记录一次并保持 `-j2` 上限。
- 不新增重量级依赖，除非现有技术栈确实无法完成且有明确跨平台/许可/离线方案；默认复用 Qt/GDAL/QGIS/PROJ/GEOS/OpenCV/现有 runtime。
- 所有输出/缓存/sidecar/数据库写入必须考虑 atomicity、cancel、失败清理、Unicode path、read-only source、NoData/CRS/provenance。
- 不把 wall-clock benchmark 当 correctness gate；规模证据优先使用内存上限、操作数/队列上限、逻辑规模和可复现 invariant。

### Model / subagent policy

**Subagents：最多 2 个，硬上限。** 建议 #1 只读做 Phase 0 基线/科学审计，#2 只读做最终对抗 review；subagent 不得再 spawn subagent，主 agent 完成全部写代码与裁决。

## Skills / method

启动时逐个确认存在再加载（不存在则跳过并记录，禁止伪称已用）：
- `.agents/skills/codebase-design/SKILL.md`：架构与边界；
- `.agents/skills/code-review/SKILL.md`：独立 review；
- `.agents/skills/diagnosing-bugs/SKILL.md`：失败根因；
- `.agents/skills/domain-modeling/SKILL.md`：authority / aggregate / schema；
- `.agents/skills/ask-matt/SKILL.md`：复杂 API/设计复核；
- `.agents/skills/implement-spec/SKILL.md` 或 `.agents/skills/implement/SKILL.md`：按 repo 实际存在情况选择；
- 冲突发生时仅在存在后加载 `.agents/skills/resolving-merge-conflicts/SKILL.md`。

同时使用 goal-loop 思路（内嵌协议，不需要修改仓库 vendoring）：
1. 在 worktree 根创建并持续维护 `.goal-loop-ledger.md`；
2. 每轮：读账本 → 找到离 Oracle 最近的真实差距 → 做一个可归因的聚焦改动 → 亲自运行验证 → 记账 → 判定；
3. 未满足 Oracle 禁止宣告 done；关键 Oracle 最终连续验证两遍；
4. 连续失败 3–4 轮必须列 3 个不同根因假设；5–7 轮换工具/层级；8+ 轮重新检查前提；
5. 只有缺外部权限/硬件等不可自动化条件才允许把某项标 `not-executed`，并继续完成其余可执行工作，不用它冒充通过。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | feature pipeline | spectral/index/texture/terrain/temporal summary 的 typed feature schema、normalization、missing/NoData。 |
| B | probabilistic classifiers | 现有 RF/SVM/Bayes 概率/decision score 一致接口，校准（Platt/isotonic 可选）与 reliability metrics。 |
| C | spatial CV 2.0 | block/group/region folds、buffer、leakage audit，与 D19 数据存储解耦。 |
| D | uncertainty | entropy/margin/ensemble disagreement、unknown/reject option、置信图。 |
| E | object-level classification | segment/object feature aggregation、majority/graph smoothing、面积/邻接规则。 |
| F | studio 11 | 概率/置信/混淆 pair/feature importance 可视化；只改 classification widget 自有文件。 |
| G | model artifact | classifier params/schema/feature fingerprint/seed/provenance，可重放。 |
| H | known-answer/e2e | synthetic spatial leak、imbalanced classes、calibration、object map、100k samples bounded。 |

每个 package 都必须包含：现状证据 → 设计选择（至少两个候选时写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。不要先堆几万行再统一测试。

## Token budget / execution phases

总预算 **500,000,000 tokens**。这是允许深入审计、实现、测试、复盘的上限/规划包线，不是追求消耗量的 KPI；禁止为了"烧满 tokens"重复阅读/重写。每个 Phase 超过预算 1.5× 时在 EVIDENCE 记录原因并继续，不向用户提问。

| Phase | 内容 | Budget |
|---:|---|---:|
| 0 | 最新态审计、ownership、architecture map、GOAL/PLAN 落盘 | 30M |
| 1 | 基础契约/数据模型/authority 层 | 80M |
| 2 | 核心算法/执行能力第一大块 | 90M |
| 3 | 核心算法/执行能力第二大块 | 75M |
| 4 | surface/integration/compatibility | 65M |
| 5 | 规模、故障、并发、性能硬化 | 55M |
| 6 | E2E、known-answer、drift/生成物验证 | 45M |
| 7 | 独立 adversarial review + 全部 P0/P1 remediation | 35M |
| 8 | 最终双验证、rebase、PR evidence/提交 | 25M |

## Autonomy defaults

1. **格式/权威来源**：优先使用 master 已有 registry/schema/domain authority；禁止建立第二份真值数据库/第二 scheduler/第二 model catalog。
2. **失败项**：先根因分析；环境缺失才标 not-executed。单测真实失败不得跳过或删 test 换绿。
3. **命名/编号**：沿用现有 namespace/operator/error/ADR 规则；冲突时选最小 additive 命名并记录。
4. **资源/超时**：build `-j2`→压力高降 `-j1`；test `-j1`；单个 scale 测试必须可通过 env/label opt-in，日常 gate 用 bounded logical scale。
5. **对外动作**：允许 `git fetch`、读取 GitHub PR/issue/review、push 自己分支、创建自己 PR；禁止 merge/close/修改他人 PR/issue，除非本 track 明确产生并拥有的新 issue（默认不创建 issue）。
6. **范围外发现**：写 EVIDENCE `OUT_OF_SCOPE`；P0 同时写 PR_BODY 顶部；不要跨到其他并行 track 大修。
7. **新依赖**：默认不用；优先现有 C++/Qt/GDAL/QGIS 实现或 optional provider seam，确保离线/Windows/Linux degradation。
8. **并发冲突**：业务代码冲突优先 rebase + 重新审计；不得为了避免冲突复制一套实现。
9. **文档与旧 backlog**：`ISSUES.md`/CHANGELOG/历史 GOAL 只做线索，所有缺口必须对当前 code 重新验证。

## GOAL Loop Oracle（未满足不得结束）

1. 空间泄漏 case 必须被 audit 捕获
2. probability/uncertainty 定义机器可验证且 class order 稳定
3. 模型 artifact 重放得到声明的 deterministic/tolerance 结果
4. classification suites 两次通过
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
6. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过（或同一明确、与本 diff 无关的 pre-existing/host limitation 被对照证明）。
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

`.goal-loop-ledger.md` 每轮格式：

```text
Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步
```

严禁用"看起来完成""其余很简单""CI 应该会过"作为结束条件。

## Required planning/evidence artifacts

除 repo `/goal` 模板要求外，本 track 至少维护：
- `CURRENT_ARCHITECTURE.md`：现状 authority/seam 图；
- `CAPABILITY_MATRIX.md`：before/after、implemented/not-supported/degraded；
- `PARALLEL_OWNERSHIP.md`：open PR/branch/file overlap；
- `TEST_MATRIX.md`：每项能力→独立 oracle→命令→exit→evidence；
- `PERFORMANCE.md`：资源模型、逻辑规模、实际 RSS/队列上限；
- `REVIEW_LOG.md`：reviewer/findings/disposition/commit/test；
- 必要时 `MIGRATION.md` / `SCHEMA.md` / `FAILURE_MATRIX.md`。

## Worktree / commit / rebase / PR runbook

在 Phase 0 只读审计完成后：

```bash
git worktree add ../exp-rs-classification-intelligence-11 -b zcode/classification-intelligence-11 origin/master
cd ../exp-rs-classification-intelligence-11
```

1. 立即把本 GOAL 原文保存到 `.planning/classification-intelligence-11/GOAL.md`，创建 `PLAN.md`、`BASELINE.md`、`DECISIONS.md`、`EVIDENCE.md`、`REVIEW_LOG.md`、`PR_BODY.md`、`PARALLEL_OWNERSHIP.md`、`TEST_MATRIX.md`。
2. 按 repo 现有模式让 `.planning/classification-intelligence-11/` 可被 git 跟踪；用 `git check-ignore -v` 验证。不要大改 `.gitignore`。
3. 每个 Phase 至少一个原子 commit；每次 commit 后记录 `git status --porcelain` 和验证结果。
4. 每个 Phase commit 后：`git fetch origin && git rebase origin/master`。冲突时先判断是否并发 track ownership；不得用 ours/theirs 粗暴覆盖科学代码。
5. 对共享注册文件（CMakeLists、registry、capability index、CHANGELOG、`.gitignore`）尽可能把修改推迟到独立 integration commit，并保持 append-only/minimal diff。
6. 完成实现后先由主 agent 全 diff review，再执行独立 reviewer；所有 P0/P1 修复后重跑 targeted gate。P2 能修则修，不能修必须逐条 disposition；P3 可记录。
7. 最后执行：`git diff --check origin/master...HEAD`、冲突标记扫描、secret 扫描、文件存在性/生成物 zero-diff 检查、targeted tests **连续两次**。
8. `git push -u origin zcode/classification-intelligence-11`；禁止 force push。
9. 创建独立 PR：`gh pr create --base master --head zcode/classification-intelligence-11 --body-file .planning/classification-intelligence-11/PR_BODY.md ...`。不要 merge，不等待线上 checks。
10. PR_BODY 必须写：baseline SHA、与当时 open PR 的 dedupe/ownership、架构决定、实际交付、兼容性、local tests、资源证据、review findings、known limitations、follow-ups、`Local evidence only; no online CI dependency`。

## Final review instructions

Review 必须从 `origin/master...HEAD` 完整 diff 开始，而不是只看最近 commit；至少覆盖：
- architecture/authority/duplication；
- science/math/CRS/units/time/NoData/provenance；
- concurrency/cancel/lifetime/atomicity/resource bounds；
- API/schema/backward compatibility/platform portability；
- test oracle independence、negative/failure/scale credibility；
- security/secret/path/remote/offline；
- UI lifecycle/accessibility（若有 UI）；
- docs/help/capability/contract drift。

修完 review finding 后必须再次 rebase `origin/master`，重跑与改动相关的 targeted gate 两遍，再创建/更新 PR。最终只报告实际完成与证据，不报告尚未执行的验证为"通过"。
