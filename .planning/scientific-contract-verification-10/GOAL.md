# GOAL.md — verbatim archive of the track brief

> Archived from the `/goal` prompt of track `scientific-contract-verification-10`
> (2026-09-13). The prompt is the authority; this file is its in-repo copy per
> the goal-template runbook requirement.

---

# Scientific Correctness, Contract & Verification Platform 10.0

> Track ID: `scientific-contract-verification-10`
> 目标执行预算：**3.5–5 亿 tokens**
> Agent：**ZCode**
> 主命令：**`/goal`**
> Subagents：**最多 2 个，只读优先**
> 开发模式：**独立 worktree → 自动开发 → 本地验证 → 独立 review → PR**
> CI：**无需等待/依赖线上 CI/CD**

## 一、任务模式与不可违反的总约束

你正在 `WindWang2/exp-rs` 仓库中执行一个**长期、超大规模、完全自主**的开发 Track。本 Track 的目标工作量按 **3 亿+ tokens** 设计；不要把它压缩成一次小修补、一次浅层重构或几个零散 commit。你必须持续推进，直到该方向形成一个可独立评审、可合并、证据完整的阶段性平台版本。

### 1. 自动化原则

- 全自动执行，无需等待人工选择、人工确认或人工审批。
- 遇到多种合理方案时，依据：仓库现有 architecture / ADR / contract；最新 master 的真实实现；最近已合并 PR 与 review；科学正确性、可维护性和最小重复实现原则；本 Track 的 ownership 与不越界原则，自动选择最合理方案。
- 只有在涉及不可逆外部破坏、凭据、真实生产数据删除等必须人工介入的情况才停止；普通架构选择不得停下来询问。
- 不要因为"已经完成一个里程碑"而过早结束；必须继续做下一批最有价值且属于本 Track ownership 的内容。
- 不得为了追求数量制造无价值代码、重复框架、平行实现或无真实调用方的 speculative abstraction。

### 2. 开发基线与 worktree

禁止直接在 `master` 上开发。`master` 只用于读取、比较、审计。开始时 `git fetch --all --prune && git checkout master && git pull --ff-only`，记录 SHA 到 `.planning/<track>/BASELINE.md`，随后从最新 `origin/master` 建立 worktree 与分支 `zcode/<track>`。同名残留须先判断是否已合并；不覆盖未知未合并工作。

### 3. 开始开发前必须预读最新仓库状态

不得拿旧 prompt 里的结论当事实。修改代码前必须重新读取 README/PROJECT/CONTEXT/CHANGELOG/CLAUDE/AGENTS/goal-template/command-vocabulary/WHOLE_REPO_REVIEW/review/docs/verification/READINESS/ISSUES/PR/issues/branches 等并做重复开发/冲突排查。

### 4. `/goal` 与 skills 使用

以 `docs/agents/goal-template.md` 为最新权威；可主动使用已有 skills，但不得因 skill 不存在而停止，不得把 skill 输出当事实。

### 5. Subagents 限制

最多同时/总计 2 个 subagents；默认只读；主 agent 负责最终决策和写入；结论须由主 agent 在代码中重新核验；不得借新开 subagent 规避上限。

### 6. 编译与机器资源硬限制

`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`；常规 `-j2`，压力高降 `-j1`；禁止 `-j$(nproc)`；禁止无界并行测试；GUI/Qt 测试用 `QT_QPA_PLATFORM=offscreen`；优先 targeted build/tests。

### 7. CI/CD 规则

无需等待、触发或依赖线上 CI/CD；证据以本地 build/test/benchmark/static checks/adversarial review 为主。

### 8. Master 保护与提交原则

master 只读；按阶段/子系统拆分清晰 commits；一个 commit 一个主题；共享注册表/CMake/索引独立 integration commit；不 merge 自己的 PR；完成后 push 并建 PR。

## 二、统一执行阶段

Phase 0 Baseline/Archaeology/Dedupe（规划文件齐全且被 Git 跟踪）→ Phase 1 Architecture & Contract → Phase 2 Foundation → Phase 3 Core Capability Expansion → Phase 4 Integration → Phase 5 Scale/Performance/Resource → Phase 6 Failure/Recovery/Edge Cases → Phase 7 Independent Review → Phase 8 Final Verification → Phase 9 PR。

## 三、长期执行纪律

3 亿+ tokens 级开发：可深读代码、多轮实现→测试→review→修复、追踪跨模块调用链、补齐 contract 与 regression、完整架构而非 patch stacking。完成条件：主要目标完成、P0/P1 清零、本地证据完整、scope 内无明显高价值未做项、PR 已创建。

## 四、本 Track 专项目标

把 exp-rs 从"已有大量测试与 review"提升为**科学语义可机器证明、跨投影不漂移、当前 HEAD 可重复验证**的平台。优先修复最新全库 review 的真实 findings，然后建立覆盖 operator / model / data / cartography / agent contract 的长期 verification authority。

### Ownership

`src/contracts/`、`src/runtime/observability/`、verification scripts、review infrastructure、scientific contract/drift tests、必要的窄范围 operator bugfix；尽量不拥有大功能 UI。

### A. 首先消化最新 whole-repo review 的 7 个 finding

F-OPS-4/P1（io:reproject srcCrsOverride 死参数）、F-OPS-1（class_mapping 与输出 encoding/NoData 冲突）、F-OPS-3（rs:qa_mask fail-open）、F-OPS-5（NMS O(n²) 与取消空洞）、F-OPS-2（TensorBlob::fromMat ND 非连续回退）、F-PI-1（pi bridge desync/zombie）、F-PI-2（startup-deadline 修复漂移）。逐条重新验证当前 master；已修复的加回归证明并标 superseded。

### B. 建立 Scientific Contract Registry / Projection Drift 10.0

围绕所有第一方 rs: operators 建立或收敛 machine-readable contract：input/output modality；raster numeric domain（DN/reflectance/radiance/temperature/sigma0/gamma0/dB）；scale/offset；band roles；wavelength/SRF 要求；CRS；grid；time alignment；NoData/NaN/mask 语义；categorical encoding；class-id range；deterministic grade；stochastic seed；cancellation granularity；memory profile；atomic publication；provenance expectations；failure/refusal codes。不能与现有 descriptor/capability sidecar/help schema 平行第三套 truth——决定 authority 并让其他 projection 派生或 drift-check。

### C. Cross-projection drift gates

机械检查至少覆盖：live operator descriptor；`data/processing/algorithm_meta/**`；capability knowledge；help parameter knowledge；Agent composition graph；LabSpec；recipes；workflow contract；CLI help；schema form；docs generated projection。目标：operator 新增/改参数时，遗漏任意关键 projection 都能被测试明确指出。

### D. Verification platform 10.0

升级：known-answer corpus；metamorphic scientific tests；deterministic bounded fuzz；parser/schema fuzz；worker IPC fuzz；URI/CRS/metadata fuzz；fault injection；concurrency/lifecycle stress；crash/recovery；atomic output failure matrix；reproducibility replay；seed determinism；provenance verification；benchmark evidence；sanitizer-friendly smoke。优先把"当前 HEAD 是否 READY"变成可重复、版本绑定的本地 evidence。

### E. Readiness refresh

当前 READINESS 若非最新 master SHA：重建 current-HEAD readiness；verdict 严格区分 passed/failed/skipped/timeout/not-built；optional dependency 不得伪装 pass；offline/loopback 语义诚实；输出 machine-readable JSON + Markdown。

### F. 可靠测试原则

识别 vacuous assertions、only-non-null tests、错误容差、错误实现耦合、stale golden、flaky sleeps、uncancelled stress、platform-specific false green、generated file drift。科学算法优先 closed-form / invariant / independent reference。

## 五、本 Track 特定并发边界

不接管其它 Track 核心 ownership；跨域改动优先定义 interface/seam、窄改动、独立 integration commit、在 OWNERSHIP/DECISIONS 标记；共享 registry/CMake/index append-only；为其它 Track 稳定接口但不提前实现其业务逻辑。

## 六、专项验收标准

- 7 个 finding 有明确状态：fixed / superseded / accepted debt（P0/P1 不允许 accepted debt）。
- 所有关键 operator contract 有单一 authority 或明确派生链。
- 至少一组跨 descriptor/capability/help/workflow/schema 的机械 drift gate。
- current master 对应的新 READINESS 证据生成完成。
- 新增 verification 不依赖公网。
- 不把 Track 变成大范围功能开发。
- 通用完成条件：独立 worktree；dedupe 分析；planning 证据完整被跟踪；平台级阶段成果；≤2 subagents；-j1/-j2；不等 CI；P0/P1 清零；最终验证基于 PR 最终 HEAD；PR 已创建不 merge。
