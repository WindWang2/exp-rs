# GOAL — F14 · Radiometric, Atmospheric & Surface Normalization 11.0

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 传感器辐射物理、大气/地形/BRDF归一化和元数据可信链
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/radiometric-physics-11`
> **Worktree:** `../exp-rs-radiometric-physics-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Mission definition

最终产品不是"写了一批代码"，而是：**传感器辐射物理、大气/地形/BRDF归一化和元数据可信链**，并且其 authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface（适用时）相互一致。先证明已有能力和真实缺口，再实现；对所有科学公式/坐标/单位/时间/NoData 语义采用独立真值，不允许测试复用被测实现来制造绿灯。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | radiometric state machine | DN/radiance/TOA/surface/temperature 的 closed vocabulary、unit/scale/offset、转换合法性与 provenance。 |
| B | solar-earth geometry | sun elevation/azimuth、earth-sun distance、acquisition time、per-pixel geometry 可选 seam。 |
| C | atmospheric provider seam | 保留 DOS/QUAC；为 LUT/外部 6S 类 provider 建立可选接口，无 provider typed-refuse。 |
| D | terrain illumination correction | cosine/C-correction/Minnaert 等明确公式、坡度/aspect/sun geometry、阴影 mask。 |
| E | BRDF/normalization seam | 可选 kernel/empirical normalization，明确所需角度元数据和适用条件。 |
| F | uncertainty/QA | 饱和、负值、cloud/shadow、invalid metadata、propagated flags。 |
| G | operator/metadata parity | schema、result、SICNU_RADIOMETRIC_STATE、scientific contracts、help。 |
| H | known-answer | Planck/TOA/geometry/topographic closed-form + missing metadata negative tests。 |

每个 package 都必须包含：现状证据 → 设计选择（至少两个候选时写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。不要先堆几万行再统一测试。

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题，不弹选项；有歧义时选择最保守、最兼容、最少重复实现的方案，并写入 `DECISIONS.md`。
- `master` 永远只读；所有编辑只发生在本 track 独立 worktree/branch。
- 不 merge 其他开发分支，不改别人的 worktree，不 force-push。
- 不等待、不重跑、不引用线上 CI 作为完成证据；PR 创建后即使自动触发 Actions 也不等待。所有 capability claim 只允许来自本地可复现 evidence。
- CMake 优先使用 `CMakePresets.json`/`build-dev`。硬资源：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；Ninja/CMake build 只能 `-j2` 或 `-j1`；禁止 `-j$(nproc)`。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；先 targeted suite，再按必要性扩大；测试 `-j1`。
- 编译期间每 60 秒记录一次 CPU/RSS/负载；无法测量时在 EVIDENCE 记录一次并保持 `-j2` 上限。
- 不新增重量级依赖，除非现有技术栈确实无法完成且有明确跨平台/许可/离线方案；默认复用 Qt/GDAL/QGIS/PROJ/GEOS/OpenCV/现有 runtime。
- 所有输出/缓存/sidecar/数据库写入必须考虑 atomicity、cancel、失败清理、Unicode path、read-only source、NoData/CRS/provenance。
- 不把 wall-clock benchmark 当 correctness gate；规模证据优先使用内存上限、操作数/队列上限、逻辑规模和可复现 invariant。

## Model / subagent policy

**Subagents：最多 2 个，硬上限。** 建议 #1 只读做 Phase 0 基线/科学审计，#2 只读做最终对抗 review；subagent 不得再 spawn subagent，主 agent 完成全部写代码与裁决。

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

1. 每个输出像元状态和单位可追溯，不出现"reflectance"含义模糊
2. 缺必要太阳/角度/校准元数据时明确拒绝或降级且说明
3. closed-form radiometric tests 两次通过
4. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
5. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过（或同一明确、与本 diff 无关的 pre-existing/host limitation 被对照证明）。
6. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

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
git worktree add ../exp-rs-radiometric-physics-11 -b zcode/radiometric-physics-11 origin/master
cd ../exp-rs-radiometric-physics-11
```

1. 立即把本 GOAL 原文保存到 `.planning/radiometric-physics-11/GOAL.md`，创建 `PLAN.md`、`BASELINE.md`、`DECISIONS.md`、`EVIDENCE.md`、`REVIEW_LOG.md`、`PR_BODY.md`、`PARALLEL_OWNERSHIP.md`、`TEST_MATRIX.md`。
2. 按 repo 现有模式让 `.planning/radiometric-physics-11/` 可被 git 跟踪；用 `git check-ignore -v` 验证。不要大改 `.gitignore`。
3. 每个 Phase 至少一个原子 commit；每次 commit 后记录 `git status --porcelain` 和验证结果。
4. 每个 Phase commit 后：`git fetch origin && git rebase origin/master`。冲突时先判断是否并发 track ownership；不得用 ours/theirs 粗暴覆盖科学代码。
5. 对共享注册文件（CMakeLists、registry、capability index、CHANGELOG、`.gitignore`）尽可能把修改推迟到独立 integration commit，并保持 append-only/minimal diff。
6. 完成实现后先由主 agent 全 diff review，再执行独立 reviewer；所有 P0/P1 修复后重跑 targeted gate。P2 能修则修，不能修必须逐条 disposition；P3 可记录。
7. 最后执行：`git diff --check origin/master...HEAD`、冲突标记扫描、secret 扫描、文件存在性/生成物 zero-diff 检查、targeted tests **连续两次**。
8. `git push -u origin zcode/radiometric-physics-11`；禁止 force push。
9. 创建独立 PR：`gh pr create --base master --head zcode/radiometric-physics-11 --body-file .planning/radiometric-physics-11/PR_BODY.md ...`。不要 merge，不等待线上 checks。
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
