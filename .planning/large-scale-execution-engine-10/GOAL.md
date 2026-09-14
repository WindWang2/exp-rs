# GOAL — large-scale-execution-engine-10 · Large-Scale Execution / External-Memory / Multi-Worker Engine 10.0

> 本文件是 `/goal` brief 的逐字存档（unattended 长跑 epic）。

/goal

## 一、任务模式与不可违反的总约束

你正在 `WindWang2/exp-rs` 仓库中执行一个**长期、超大规模、完全自主**的开发 Track。本 Track 的目标工作量按 **3 亿+ tokens** 设计；不要把它压缩成一次小修补、一次浅层重构或几个零散 commit。你必须持续推进，直到该方向形成一个可独立评审、可合并、证据完整的阶段性平台版本。

### 1. 自动化原则

- 全自动执行，无需等待人工选择、人工确认或人工审批。
- 遇到多种合理方案时，依据：
  1. 仓库现有 architecture / ADR / contract；
  2. 最新 `master` 的真实实现；
  3. 最近已合并 PR 与 review；
  4. 科学正确性、可维护性和最小重复实现原则；
  5. 本 Track 的 ownership 与不越界原则；
  自动选择最合理方案。
- 只有在涉及不可逆外部破坏、凭据、真实生产数据删除等必须人工介入的情况才停止；普通架构选择不得停下来询问。
- 不要因为"已经完成一个里程碑"而过早结束；必须继续做下一批最有价值且属于本 Track ownership 的内容。
- 不得为了追求数量制造无价值代码、重复框架、平行实现或无真实调用方的 speculative abstraction。

### 2. 开发基线与 worktree

禁止直接在 `master` 上开发。`master` 只用于读取、比较、审计。

开始时必须：

```bash
git fetch --all --prune
git checkout master
git pull --ff-only
```

确认 `origin/master` 最新 SHA，并记录到本 Track 的 `.planning/<track>/BASELINE.md`。

随后从**最新 `origin/master`** 建立本 Track 独立 worktree 和独立分支：

```bash
git worktree add ../exp-rs-<track> -b zcode/<track> origin/master
```

如果同名分支或 worktree 已存在：
- 先检查它是否是历史已合并残留；
- 不得覆盖未知未合并工作；
- 历史已合并残留可换一个新的、语义明确的分支名；
- 记录判断依据。

### 3. 开始开发前必须预读最新仓库状态

**不得拿旧 prompt 里的结论当事实。**

在修改代码前，必须重新读取：

- 当前 `origin/master`；
- `README.md`、`PROJECT.md`、`CONTEXT.md`、`CHANGELOG.md`、`CLAUDE.md`、`.agents/AGENTS.md`；
- 最新 `docs/agents/goal-template.md`、`docs/agents/command-vocabulary.md`；
- `WHOLE_REPO_REVIEW.md`；
- `review/` 下与本 Track 相关的 findings / issue drafts / coverage / dedupe；
- `docs/verification/READINESS.md` 与对应 SHA；
- `ISSUES.md`；
- 当前 GitHub Open/Closed PR、Issues；
- 最近至少 30 个与本 Track ownership 有交集的 PR；
- 当前远端 branches，判断哪些只是已合并残留；
- 本 Track 相关 ADR、tests、benchmarks、schemas、catalogs、help、capability metadata。

必须专门做一次**重复开发/冲突排查**：
- 已合并功能不得重做；
- 活跃或历史分支里已经完成且已并入 master 的内容不得重新实现；
- 最新 review 中明确认定已修复的问题不得再次作为主要交付；
- 如果发现另一个 Track 正在修改共享文件，尽量通过 append-only 或窄集成 commit 降低冲突。

### 4. `/goal` 与 skills 使用

以当前仓库里的 `docs/agents/goal-template.md` 为最新权威；如果本 prompt 与 repo-local `/goal` 模板在纯格式上存在差异，以 repo-local 模板补齐格式，但**不得弱化这里的硬约束**。

可主动使用仓库/用户环境中已有 skills，但不得因为 skill 不存在而停止；不得把 skill 输出当事实，必须回到真实代码验证。

### 5. Subagents 限制

**最多同时/总计启动 2 个 subagents。**

建议用途：
- Subagent A：只读架构/科学正确性/历史去重审查；
- Subagent B：只读对抗 review / 测试可信度 / 性能与并发审查。

约束：
- subagents 默认只读；
- 主 agent 负责最终决策和写入；
- 不要让两个 subagents 修改同一批文件；
- 任何 subagent 结论都要由主 agent 在代码中重新核验；
- 不允许通过"不断新开 subagent"规避 2 个上限。

### 6. 编译与机器资源硬限制

本项目编译体量很大，必须保护主机资源。

默认：
```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=1
```

规则：
- 常规编译 `-j2`；
- 重型目标、链接、内存压力高时降为 `-j1`；
- 禁止 `-j$(nproc)`；
- 禁止无界并行测试；
- GUI/Qt 测试优先 `QT_QPA_PLATFORM=offscreen`；
- 优先 targeted build / targeted tests，再决定是否做更大范围验证；
- 若机器 load / RSS 明显升高，主动降为 `-j1`；
- 不以"跑全仓库所有测试"为形式主义目标；重点是高价值、与变更相关、可归因的本地验证。

### 7. CI/CD 规则

- **无需等待、触发或依赖线上 CI/CD。**
- 不要把 GitHub Actions 绿色当完成条件。
- 不要因为线上 CI 未运行而停住。
- 证据以本地 build / test / benchmark / static checks / adversarial review 为主。
- 如果发现 repo 现有 CI 配置问题，只有在本 Track ownership 直接相关时才修；否则记录，不扩大 scope。

### 8. Master 保护与提交原则

- `master` 只读。
- 所有开发只在本 Track worktree。
- 按阶段/子系统拆分清晰 commits。
- 一个 commit 只解决一个高内聚主题。
- 不要把大规模格式化、无关 rename 混入功能 commit。
- 共享注册表/CMake/索引文件尽量放在独立 integration commit。
- 不要 merge 自己的 PR。
- 完成后 push branch 并创建 PR 到 `master`。

---

## 二、统一执行阶段

### Phase 0 — Baseline / Archaeology / Dedupe（本文件 §四.0 + 规划文件落盘）

### Phase 1 — Architecture & Contract
先定义契约再大改代码：domain model；error taxonomy；typed result/refusal；schema/version strategy；compatibility strategy；determinism；memory/cancellation；provenance；persistence/migration（如适用）；UI/CLI/Agent projection（如适用）。重要决策写 ADR 或 `DECISIONS.md`。

### Phase 2 — Foundation
先实现公共基础，不要复制已有实现：优先复用现有 canonicalizer、Result/Diagnostic、TaskCenter、JobEngine、RasterReader、Workspace、Experiment、capability knowledge、Help、MapSpec、Model Runtime 等真实 authority。

### Phase 3 — Core Capability Expansion
实施本 Track 的主要功能集。每一批功能都必须：有真实调用路径；有 schema/contract；有错误语义；有测试；有文档或 machine-readable metadata。

### Phase 4 — Integration
至少核查：GUI；CLI；MCP/Pi/Agent；workflow/pipeline；help；capability metadata；data/provenance；tests；docs；packaging（如果相关）。禁止 UI/CLI/Agent 各自重新实现业务逻辑。

### Phase 5 — Scale / Performance / Resource
synthetic / sparse / generated data；验证复杂度与 bounded-memory contract；性能数字是 evidence，不做脆弱 wall-clock gate；热点必须 profile/benchmark 后再优化。

### Phase 6 — Failure / Recovery / Edge Cases
empty / single / all-NoData；NaN / Inf；corrupt metadata；invalid CRS/grid；cancellation；timeout；partial output；crash/restart；stale state；duplicated id；malformed JSON；incompatible versions；offline mode；platform differences；very large logical datasets。

### Phase 7 — Independent Review
完成主要开发后，必须停止新增功能，进行独立 review。最多 2 个只读 subagents。P0/P1 必须全部修复；高价值 P2 应尽量修复；`REVIEW_LOG.md` 记录 finding → disposition → evidence。

### Phase 8 — Final Verification
重新基于最终 HEAD 做：targeted build；targeted tests；relevant regression；schema/catalog drift checks；deterministic generation checks；benchmark/scale evidence；`git diff --check`；conflict-marker scan；secret scan（如适用）；source/test/docs claim audit。不得引用修改前的旧测试结果作为最终结果。

### Phase 9 — PR
rebase / update 到最新 `origin/master`（在安全前提下）；解决冲突；对冲突区域重新跑相关验证；更新 planning evidence；push；创建 PR（body 含 baseline/architecture/deliverables/compatibility/tests/performance/review findings/known limitations/follow-ups/"local evidence only; no online CI dependency"）。PR 创建完成后，**不要 merge PR**。

---

## 三、长期执行纪律

本 Track 是 3 亿+ tokens 级开发：可以深读足够多代码；可以多轮实现→测试→review→修复；可以追踪跨模块真实调用链；可以补齐长期缺失的 contract 和 regression；可以做完整架构而不是 patch stacking。

只有当：主要目标完成；P0/P1 清零；本地证据完整；scope 内没有明显高价值未做项；PR 已创建；才视为完成。

---

## 四、本 Track 专项目标

把当前稳定的 TaskCenter/JobEngine/worker execution plane 升级为能可靠执行超大遥感 DAG 的外存/分块/多 worker 引擎：bounded memory、backpressure、checkpoint/resume、content cache、CPU/GPU/IO admission 和 crash recovery。

### Ownership

TaskCenter、JobEngine、worker/runtime、execution cache、artifact/scratch/checkpoint、resource scheduling；不拥有科学算法公式。

### 专项开发内容

**A. Execution architecture baseline** — 逐条梳理 TaskCenter、JobEngine、ExecutionPlane、WorkflowRunCoordinator、worker host/pool、plugin worker、artifact store、output committer、scratch、execution cache、fingerprint、checkpoint、cancellation、retry、trace、resource budget。特别复核最近历史中：锁内 I/O；resolver under mutex；instant completion race；worker child wait；IPC framing；plugin host lifecycle——这些已修问题不得回归。

**B. Tile DAG / Stream execution** — 设计能表达：raster tile；halo；band subset；time chunk；dependency；producer/consumer；tile lifetime；reusable intermediate；deterministic partition。建立 capability contract：streamable；full-raster；two-pass；neighborhood/halo；global-reduction；external-memory。不要强迫所有 operator 立刻 tile 化。

**C. Memory planner** — 在 submission/admission 前估算 working set、input window、output、halo、concurrency、GPU VRAM、scratch。支持 reduce concurrency、spill、refuse with actionable estimate；no `bad_alloc` as normal control flow。

**D. Backpressure** — bounded queues；producer throttling；remote I/O throttling；disk writer throttling；GPU admission；worker saturation；cancellation propagation；no deadlocks。

**E. External-memory algorithms infrastructure** — 通用 primitives：tiled scan；multi-pass reduction；disk-backed intermediate；external sort/merge（若实际需要）；chunked table；scratch cleanup；temp naming；atomic finalize。不要把科学算法重复搬进 runtime。

**F. Content-addressed execution cache** — 扩展现有 cache/fingerprint：input identities；operator implementation hash；params；environment relevant pins；deterministic grade；output digest；stale validation；cache refusal for stochastic/non-reproducible ops；partial cache corruption recovery。

**G. Checkpoint / Resume** — workflow 与 long task 两层区分：completed step reuse；in-progress resumable capability；checkpoint version；atomic writes；crash restart；stale checkpoint detection；parameter/input drift；user cancellation vs crash。

**H. Resource scheduler** — 统一 CPU thread slots、RAM、GPU、VRAM、IO weight、external process、exclusive resource、remote provider quota。和 Model Runtime 现有 device truth 对接，不建立第二份 GPU detector。

**I. Worker model** — 强化 local isolated workers：heartbeat；health；crash；poison task；bounded respawn；orphan cleanup；idempotent dispatch；duplicate completion handling；process group cleanup。可定义 remote-worker protocol seam，但不要夸大成 distributed cluster。

**J. Scale tests** — synthetic：100k task DAG；deep chains；wide fan-out；cancel storm；worker crash storm；bounded scratch；cache hit/miss；checkpoint restart；10^6 logical tiles；resource starvation；no unbounded resident state。

## 五、本 Track 特定并发边界

- 不主动接管其它 Track 的核心 ownership。
- 跨域改动优先定义 interface/seam；共享文件做窄改动；单独 integration commit；在 `OWNERSHIP.md` 与 `DECISIONS.md` 标记原因。
- 不重做已经由其它 Track 或最新 master 完成的实现。
- 对共享 registry/CMake/index 的更改尽量 append-only。

## 六、专项验收标准

- 大 workflow 不再依赖"所有 intermediate 常驻内存"。
- admission 对 CPU/RAM/GPU/IO 有明确 contract。
- cache/checkpoint/resume 可证明一致性。
- worker crash/cancel/duplicate callback 不破坏 terminal semantics。
- scale tests 验证复杂度而非依赖巨型真实文件。

通用完成条件：
- [ ] 从最新 `origin/master` 创建独立 worktree。
- [ ] 完成最新 PR/issues/review/branches 去重分析。
- [ ] `.planning/large-scale-execution-engine-10/` 证据完整并被 Git 跟踪。
- [ ] 主要交付是平台级阶段成果。
- [ ] 最多 2 个 subagents。
- [ ] 编译严格 `-j1/-j2`，测试默认串行或极低并行。
- [ ] 不等待线上 CI/CD。
- [ ] P0/P1 review findings 全部清零。
- [ ] 最终本地验证基于 PR 最终 HEAD。
- [ ] PR 已 push 并创建到 `master`。
- [ ] PR 不由本 Track 自动 merge。
