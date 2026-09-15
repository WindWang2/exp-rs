# GOAL — F17 · Geospatial I/O, COG & Interchange Platform 11.0（原文存档）

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 原子输出、COG/向量云格式、sidecar/provenance和可恢复I/O基础
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/geospatial-io-formats-11`
> **Worktree:** `../exp-rs-geospatial-io-formats-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Prompt-generation snapshot（只用于启动审计，不是固定基线）

本 Prompt 生成时（2026-09-15）观测到：
- `origin/master` = `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`。
- open PR #991 `grok/unified-mission-workbench-d18`；open PR #992 `grok/dataset-foundry-benchmark-d19`。
- 当时无独立 open issues；`ISSUES.md` 是旧 D3 backlog，多数条目已被后续 10.0 PR 修复，禁止把它当实时 backlog 直接实施。
- 最近 master 已合入 D14–D17 与 Data Fabric / Verification / Spectral / Execution / EO Model / Workbench 等 10.0 平台能力。

**启动时必须完全刷新这些事实。任何 SHA/PR 状态发生变化，以启动时 GitHub/origin 事实为准。**

## Why this track now

近期 #990 刚修正 GDAL WKT API 编译错误，说明 I/O surface 仍是高风险基础层；可进一步统一 atomic writer、COG、可恢复 stage、vector interchange、metadata/sidecar 和 GDAL capability gates。

本段只是生成 Prompt 时的方向判断。执行者必须以 Phase 0 重新读取的 master/PR/issues/reviews/code 为准；如果现状已经覆盖某个 package，不重复造轮子，而是在本领域内向下一个可验证缺口深化，并把 rescope 写入 `DECISIONS.md`。

## Phase 0 必做：在创建本 track worktree 前读取最新 master / PR / issue / review / branch

（原文含完整命令清单：git fetch origin --prune / rev-parse origin/master / log -20 / branch -r；gh pr list / pr view / pr diff --name-only；gh issue list；sed ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md；建立 PARALLEL_OWNERSHIP.md 及其 5 条规则 — 详见本文件版本库上游 prompt。）

规则摘要：
1. 仍开放 PR 的 changed files 默认 read-only，不得复制/重做其功能；
2. 依赖 open PR 新 API 时优先消费 master 已有稳定 seam；否则改为 adapter/contract/test scaffold 并标 follow-up；
3. 该 PR 已合并则以新 origin/master 重新审计；
4. 新并发 PR 同样适用；
5. open issue 逐条 dedupe，已修复未关的要记录证据，不重复实现。

## Ownership / parallelism

**Primary write scope（启动审计后可收窄，不可无理由扩大）：**
- `src/geospatial/io/**`
- `src/operators/io/**`
- `src/processing/gdal/**`
- `tests/*io*`
- `docs/io/**`

**默认 read-only / 避免并发冲突：**
- src/geospatial/fabric/** owned by F05
- G01 runtime internals
- F02 product-specific parsers

并行开发原则：业务主体必须落在 primary scope；共享 integration files 只做最小 append-only 接线；发现 open PR 已在相同业务主体开发时，优先消费 master 上已有稳定 seam，禁止“同功能换名字再写一遍”。

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题，不弹选项；歧义时选最保守、最兼容、最少重复实现方案并写入 DECISIONS.md。
- `master` 永远只读；所有编辑只发生在本 track 独立 worktree/branch。
- 不 merge 其他开发分支，不改别人的 worktree，不 force-push。
- 不等待、不重跑、不引用线上 CI；所有 capability claim 只允许来自本地可复现 evidence。
- CMake 优先 `CMakePresets.json`/`build-dev`；`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；build 只能 `-j2`/`-j1`；RSS>70% 降 `-j1`。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；先 targeted suite；测试 `-j1`。
- 编译期间每 60 秒记录 CPU/RSS/负载；无法测量时在 EVIDENCE 记录一次并保持 `-j2` 上限。
- 不新增重量级依赖；默认复用 Qt/GDAL/QGIS/PROJ/GEOS/OpenCV/现有 runtime。
- 所有输出/缓存/sidecar/数据库写入必须考虑 atomicity、cancel、失败清理、Unicode path、read-only source、NoData/CRS/provenance。
- 不把 wall-clock benchmark 当 correctness gate；规模证据优先内存上限、操作数/队列上限、逻辑规模和可复现 invariant。

### Model / subagent policy

Subagents：最多 2 个，硬上限。#1 只读做 Phase 0 基线/科学审计，#2 只读做最终对抗 review；subagent 不得再 spawn；主 agent 完成全部写代码与裁决。

## Skills / method

逐个确认存在再加载：codebase-design / code-review / diagnosing-bugs / domain-modeling / ask-matt / implement-spec 或 implement；冲突时 resolving-merge-conflicts。同时使用 goal-loop 内嵌协议（`.goal-loop-ledger.md` 每轮：读账本 → 找最近差距 → 单一聚焦改动 → 亲自验证 → 记账 → 判定；未满足 Oracle 禁止宣告 done；关键 Oracle 连续验证两遍；连续失败 3–4 轮列 3 个根因假设；5–7 轮换工具/层级；8+ 轮重查前提；仅外部不可自动化条件可标 not-executed）。

## Mission definition

最终产品：**原子输出、COG/向量云格式、sidecar/provenance和可恢复I/O基础**，authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface 相互一致。先证明已有能力和真实缺口再实现；科学公式/坐标/单位/时间/NoData 语义用独立真值，不允许测试复用被测实现制造绿灯。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | atomic dataset writer 2 | stage/finalize/rollback、fsync/rename、sidecars、digest、partial cleanup、attach-existing public contract。 |
| B | COG production | tiling/compression/overview/blocksize、validation、BigTIFF、NoData/alpha、deterministic options。 |
| C | vector interchange | GPKG/GeoJSON/FlatGeobuf/GeoParquet 仅在 GDAL driver 可用时 capability-gated；schema/CRS/geometry/encoding。 |
| D | multidataset/subdataset | HDF/NetCDF/VRT/subdataset inventory、safe URI、selection、metadata projection。 |
| E | metadata truth | scale/offset/unit/band roles/wavelength/time/QA/provenance roundtrip。 |
| F | resume/repair seam | 给 G01/Model Runtime 提供 attach-existing/validate stage API，不在本 track 实现 scheduler。 |
| G | security/path robustness | Unicode/long path/read-only/relative traversal/VSI credentials/redacted errors。 |
| H | GDAL version matrix tests | feature detection、old/new API compatibility、corrupt/truncated/huge logical dataset。 |

每个 package：现状证据 → 设计选择（≥2 候选写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。不要先堆几万行再统一测试。

## Token budget / execution phases

总预算 **500,000,000 tokens**（规划包线，非 KPI）。

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

1. 优先 master 已有 registry/schema/domain authority；禁止第二份真值/第二 scheduler/第二 model catalog。
2. 失败项先根因分析；环境缺失才标 not-executed；单测真实失败不得跳过或删 test 换绿。
3. 沿用现有 namespace/operator/error/ADR 规则；冲突时最小 additive 命名并记录。
4. build `-j2`→压力高降 `-j1`；test `-j1`；scale 测试 opt-in，日常 gate 用 bounded logical scale。
5. 对外动作：允许 git fetch、读 GitHub、push 自己分支、创建自己 PR；禁止 merge/close/修改他人 PR/issue；默认不创建 issue。
6. 范围外发现写 EVIDENCE `OUT_OF_SCOPE`；P0 同时写 PR_BODY 顶部。
7. 新依赖默认不用；optional provider seam，离线/Windows/Linux degradation。
8. 并发冲突优先 rebase + 重新审计；不得复制实现避冲突。
9. ISSUES.md/CHANGELOG/历史 GOAL 只做线索，缺口必须对当前 code 重新验证。

## GOAL Loop Oracle（未满足不得结束）

1. 失败/取消后不会把不完整文件当成功输出
2. COG validator 对 shipped fixture 通过且参数可解释
3. driver 不可用时 skip/refusal 有明确原因而非构建失败
4. I/O suites 两次通过
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；生成物/manifest drift gate clean
6. Phase 8 关键 targeted validation 原样连续运行两遍，两次通过（或同一明确、与本 diff 无关的 pre-existing/host limitation 被对照证明）
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge

`.goal-loop-ledger.md` 每轮格式：

```text
Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步
```

严禁用“看起来完成”“其余很简单”“CI 应该会过”作为结束条件。

## Required planning/evidence artifacts

CURRENT_ARCHITECTURE.md / CAPABILITY_MATRIX.md / PARALLEL_OWNERSHIP.md / TEST_MATRIX.md / PERFORMANCE.md / REVIEW_LOG.md；必要时 MIGRATION.md / SCHEMA.md / FAILURE_MATRIX.md。

## Worktree / commit / rebase / PR runbook

Phase 0 审计后：worktree add → 保存 GOAL 原文 + 创建 PLAN/BASELINE/DECISIONS/EVIDENCE/REVIEW_LOG/PR_BODY/PARALLEL_OWNERSHIP/TEST_MATRIX → .planning 可跟踪验证 → 每 Phase ≥1 原子 commit + status 记录 → 每 Phase commit 后 fetch+rebase origin/master → 共享注册文件推迟独立 integration commit 保持 append-only → 主 agent 全 diff review + 独立 reviewer，P0/P1 修复后重跑 gate → diff --check、冲突标记扫描、secret 扫描、生成物 zero-diff、targeted tests 连续两次 → push（禁 force）→ gh pr create（不 merge、不等 checks）→ PR_BODY 含 baseline SHA/dedupe/架构/交付/兼容/local tests/资源/review/limitations/follow-ups/`Local evidence only; no online CI dependency`。

## Final review instructions

Review 从 `origin/master...HEAD` 完整 diff 开始：architecture/authority/duplication；science/math/CRS/units/time/NoData/provenance；concurrency/cancel/lifetime/atomicity/resource bounds；API/schema/backward compat/portability；test oracle independence、negative/scale credibility；security/secret/path/remote/offline；UI lifecycle（若有）；docs/help/capability/contract drift。修完 finding 再次 rebase、重跑 targeted gate 两遍、再创建/更新 PR。最终只报告实际完成与证据。
