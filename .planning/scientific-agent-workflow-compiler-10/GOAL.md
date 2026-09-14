# GOAL — scientific-agent-workflow-compiler-10 · Scientific Agent Workflow Compiler / Autonomous RS Harness 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/scientific-agent-workflow-compiler-10` (worktree
> `../exp-rs-scientific-agent-workflow-compiler-10`, off `origin/master` @ `7d78059d1a6d316d606656759a506d17bc5e3b55`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on,
> trigger, or cite online CI.
> **Write scope:** `src/agent/harness/` (new compiler/IR/analysis/repair/planner files +
> narrow integrations), `data/agent/evals/` (new cases), `data/agent/capabilities/` +
> `data/processing/algorithm_meta/capability/` (append-only knowledge for repair contracts),
> `pi/` (drift guard + the two P2 findings F-PI-1/F-PI-2 owned by this track),
> `tests/` (new suites), `docs/adr/` (one new ADR), `docs/` notes this track owns,
> `.gitignore` (one whitelist entry), `.planning/scientific-agent-workflow-compiler-10/`.
> **Read-only:** `master`, `src/operators/` (algorithms track), `src/processing/` framework
> internals (execution-plane tracks), `src/workflow/` engine (authoritative plane — consume,
> never fork), `src/geospatial/` (data-fabric track), `review/` dossiers (authority).

## Mission (archived brief — full text)

把当前 capability knowledge + relation graph + spatial scientist harness 升级为真正的
**Scientific Workflow Compiler**：从用户科学意图生成 typed WorkflowIR，静态验证
数据/CRS/grid/bands/time/numeric-domain/resources，再执行、修复、保持情境并产出证据。

### 总约束（不可违反）

- 长期、超大规模、完全自主；直到形成可独立评审、可合并、证据完整的阶段性平台版本。
- 全自动：架构选择按 1) 仓库 ADR/contract 2) 最新 master 真实实现 3) 最近已合并 PR 与
  review 4) 科学正确性/可维护性/最小重复 5) ownership 边界自动决定，写入 DECISIONS.md。
- 禁止直接在 `master` 开发；worktree + 独立分支。
- 开始前预读最新仓库状态，重复开发排查：已合并功能不重做、活跃分支已完成不重做、
  最新 review 认定已修复不作为主要交付。
- 最多 2 个 subagents，只读；主 agent 100% 决策与写入。
- 编译硬限制：`CMAKE_BUILD_PARALLEL_LEVEL=2`，常规 `-j2`，压力降 `-j1`，禁 `-j$(nproc)`；
  `CTEST_PARALLEL_LEVEL=1`；`QT_QPA_PLATFORM=offscreen`；targeted 优先。
- 不等待/不触发/不依赖线上 CI；证据以本地 build/test/benchmark/static check/adversarial
  review 为准，进 EVIDENCE.md。
- 一个 commit 一个高内聚主题；共享注册表/CMake/索引改动独立 integration commit；
  不 merge 自己的 PR。

### 四、本 Track 专项目标（全文）

**Ownership**: `src/agent/harness/`、capability/knowledge/composition、WorkflowIR/static
analysis、context ledger、Agent execution/evidence；算法实现本身归各科学 Track。

**A. Harness baseline** — 完整审查 intent vocabulary、AgentPlan、capability knowledge、
capability graph、recipe catalog、solution templates、scientific preflight、context ledger、
grounding、harness actions、MCP/Pi、lab copilot、error taxonomy、evidence、repair loop。
不得重新发明 111 operator capability catalog。

**B. WorkflowIR 10.0** — 设计 typed IR，至少表达：step/node id、operator/tool、typed
inputs/outputs、raster/vector/table/model/structured artifact、CRS、grid、band roles、
wavelengths、numeric domain、temporal domain、sensor/modality、deterministic grade、
resource estimate、device requirement、provenance/evidence expectation、user-visible
semantic output。IR 必须 versioned、serializable、bounded、可 deterministic normalize。

**C. Static Analysis** — 执行前 compiler-like checks：unknown operator、missing params、
type mismatch、modality mismatch、band-role mismatch、wavelength incompatibility、CRS
mismatch、grid mismatch、temporal misalignment、linear/dB、DN/reflectance、categorical
encoding、model input incompatibility、resource over-budget、output path collision、
non-deterministic chain warning。

**D. Deterministic repair insertion** — 事实足够时自动插入已有能力：align、reproject、
resample、scale/calibrate、QA mask、format conversion、band select/reorder、temporal
normalize、data staging。规则 capability/contract 驱动，不是 prompt 猜测。任何可能改变
科学意义的修复必须明确记录、必要时 refuse、不 silent auto-fix。

**E. Planner** — `Intent → Goal → data understanding → candidate methods → WorkflowIR →
static validation → executable plan` 拆成可检查阶段。支持 alternative plans、explain why、
prerequisite list、missing data、limitation、cost、deterministic ranking。LLM 负责语义
推理，能确定的事实尽量由 deterministic services 提供。

**F. Context / Situation Management** — 当前 project、selected AOI、datasets、active
results、prior decisions、failed attempts、user constraints、current map state、model/tool
capabilities、pending questions、evidence。长任务：checkpoint、resume、repair attempts、
context compaction、stale context invalidation、artifact identity。

**G. Execution / Repair Loop** — typed loop：preflight、execute、observe、diagnose、
repair、bounded retry、verify、continue。禁止无限 retry、相同错误重复、只靠 error string
prefix、丢失 original intent、失败后重新从零规划。

**H. Tool / Knowledge token budget** — capability manifest、error catalog、help summary
有明确 token/char budget；按需检索；不把 111 operators 全部塞进每次 prompt；
deterministic filtering；relevant tool shortlist；provenance of why included。

**I. Scientific Evidence** — 最终结果回答：使用了哪些数据、为什么选这些算法、哪些自动
修复、关键参数来源、哪些假设、哪些限制、是否 deterministic、验证了什么、产物在哪里。

**J. Pi bridge consistency** — 结合最新 review（F-PI-1 失步僵尸流、F-PI-2 startup
deadline 漂移）：Pi 实际加载文件、bridge startup timeout、frame synchronization、
cancellation、tool list、context、duplicate implementations；建立 drift guard 避免 TS
两实现长期分叉。

**K. Evaluation corpus** — 扩展 harness eval：ambiguous intent、under-specified data、
wrong band、wrong CRS、dB/linear、missing sensor metadata、temporal mismatch、resource
pressure、tool failure、repair success、repair refusal、long-context continuation、
no-tool answer、teaching mode boundaries。可 deterministic 判定的尽量 deterministic。

### 并发边界

- 不接管其它 10.0 Track 的核心 ownership；跨域改动优先 interface/seam、窄改动、
  独立 integration commit，OWNERSHIP.md + DECISIONS.md 记录。
- 不重做其它 Track/最新 master 已完成的实现；共享 registry/CMake/index 尽量 append-only。
- 其它 Track 未来需要的接口优先稳定接口，不提前实现对方业务逻辑。

### 专项验收标准

- Agent 不再只输出"工具调用序列"，而是 typed WorkflowIR。
- 运行前能发现多数结构/科学 contract 错误。
- 自动修复有显式 evidence，不静默改变科学语义。
- 多轮长任务 context 可 checkpoint/resume。
- Pi/MCP/GUI 共享 harness authority。

### 通用完成条件

- [x] 从最新 `origin/master` 创建独立 worktree。
- [ ] 完成最新 PR/issues/review/branches 去重分析。
- [ ] `.planning/scientific-agent-workflow-compiler-10/` 证据完整并被 Git 跟踪。
- [ ] 主要交付是平台级阶段成果。
- [ ] 最多 2 个 subagents。
- [ ] 编译严格 `-j1/-j2`，测试默认串行。
- [ ] 不等待线上 CI/CD。
- [ ] P0/P1 review findings 全部清零。
- [ ] 最终本地验证基于 PR 最终 HEAD。
- [ ] PR 已 push 并创建到 `master`，不由本 Track merge。

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended; decisions recorded in DECISIONS.md, never asked.
- **Subagents ≤ 2, read-only**: #1 = Phase 7 架构/科学正确性审查；#2 = Phase 7
  测试可信度/边界/并发/文档-代码一致性审查。不得再派生。
- **No CI**: local evidence only → EVIDENCE.md.
- **Build**: preset `build-dev`（或既有 ci-fast profile）；`-j2` 降级 `-j1`；
  `QT_QPA_PLATFORM=offscreen`；targeted `ctest -R <family> -j1`。
- **Exit**: PR created, not merged.

## Autonomy defaults

1. **格式/来源**: IR 事实源 = CapabilityKnowledge/CapabilityCatalog/CapabilityRelations +
   DatasetUnderstanding（spatial:understand）；IR 自身 schema 版本化 `1`。
2. **失败项处置**: 单条目验证失败 → fail-closed 拒绝该条目并记录 loadProblems，不中断整体。
3. **命名/编号**: 新文件 `workflow_ir*` / `workflow_analysis*` / `workflow_repair*` /
   `workflow_planner*` / `context_checkpoint*` 前缀；ADR 取 `0149`（0148 已被 temporal
   track 认领）；测试 `test_workflow_compiler*.cpp`。
4. **资源与超时**: 单条 build 命令 ≤ 10 min 超时守护；测试 targeted 串行。
5. **对外动作**: `git fetch`/`gh pr create`/`git push -u origin <branch>` 允许；其余只读。
6. **范围外发现**: 记 EVIDENCE.md `OUT_OF_SCOPE`（如 F-OPS-3 qa_mask fail-open 归算法
   Track）；P0 级另标 PR_BODY.md 顶部。
7. **依赖新增**: 不新增任何第三方依赖；全部用现有 jsoncpp/Qt/STL。

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| WP1 | WorkflowIR core | `workflow_ir.{h,cpp}`: typed nodes/ports/artifacts, versioned JSON, normalize, fingerprint, bounds |
| WP2 | Static analysis | `workflow_analysis.{h,cpp}`: 16 check families, typed issues, severity, repairability |
| WP3 | Repair insertion | `workflow_repair.{h,cpp}`: contract-driven repair table, evidence records, refusals |
| WP4 | Planner stages | `workflow_planner.{h,cpp}`: staged pipeline → IR → validated executable plan + alternatives |
| WP5 | Context checkpoint | `context_checkpoint.{h,cpp}` + ContextLedger serialization: checkpoint/resume/compaction/stale |
| WP6 | Execution loop deepening | typed loop state machine over run_loop + bounded retry + same-error guard |
| WP7 | Tool shortlist budget | deterministic shortlist w/ provenance + budgets (H) |
| WP8 | Pi bridge drift guard | shared-tool-list guard + F-PI-1/F-PI-2 fixes + pi tests |
| WP9 | Eval corpus + evidence | compiler eval cases + evidence IR projection (I) |
| WP10 | Review/PR | 2 subagent reviews, fixes, final verification, PR |

## Required planning files

`GOAL.md · PLAN.md · BASELINE.md · DECISIONS.md · EVIDENCE.md · REVIEW_LOG.md ·
ARCHITECTURE.md · CAPABILITY_MATRIX.md · OWNERSHIP.md · MILESTONES.md · PROGRESS.md ·
PERFORMANCE.md · PR_BODY.md` — all under `.planning/scientific-agent-workflow-compiler-10/`.
