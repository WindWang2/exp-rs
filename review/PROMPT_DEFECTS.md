# Prompt & Command Hygiene — 缺陷清单与事实标准约定

Track: prompt-command-hygiene-review · Phase 2 · 2026-09-13
五透镜：1 完整性 · 2 可判定性 · 3 单一事实来源 · 4 边界清晰度 · 5 落地可行性。
每条缺陷含逐字引用；引不出证据的候选已撤下（见文末"撤下记录"，计入假阳性率）。

---

## Part I · 缺陷清单

### D-001 · AGENTS.md 的出处指针指向不存在的技能

- **Lens**: 3
- **Location**: `.agents/AGENTS.md:3`
- **Quoted**: `All AI agents assisting with coding, reviewing, or refactoring in this repository must strictly adhere to the following core guidelines derived from the [Karpathy Guidelines](file:///.agents/skills/karpathy-guidelines/SKILL.md).`
- **Defect**: 断链引用
- **Impact**: 全仓库 agent 行为准则的唯一权威来源，第一行就是死链接。agent 顺着指针找"原始准则"会失败；技能路由器（ask-matt）也列不出该技能。zcode 与 Claude Code 两个运行时每次会话都加载这条断链。
- **Verified**: verified-by-execution（`ls .agents/skills/karpathy-guidelines/` → No such file or directory）
- **Recommended fix**: 已证实出处实为 Andrej Karpathy 四准则（`CHANGELOG.md:1016`："Configured project-scoped behavioral rules integrating Andrej Karpathy's 4 core guidelines (Think Before Coding, Simplicity First, Surgical Changes, Goal-Driven Verification)."）。最小修复：删除死指针，出处改指 CHANGELOG 条目；四原则本已内联，无需重建技能（DECISIONS D-004/D-005）。本 track Phase 6 实施。

### D-002 · "/goal vocabulary" 被引用但从未落盘

- **Lens**: 3
- **Location**: `.planning/geospatial-data-fabric-9/ISSUE_TRIAGE.md:4`
- **Quoted**: `Method: every open issue re-located on the latest master code, not old line numbers. Classification per /goal vocabulary.`
- **Defect**: 断链引用 / 约定缺失
- **Impact**: 读 triage 的人/agent 无法知道 classification 词表是什么；该引用是全仓唯一一处，指向一个不存在的文件。后续 track 无法复现同一套分类。
- **Verified**: verified-by-execution（`grep -rn "/goal vocabulary" .planning/` 仅此一处；无对应文件）
- **Recommended fix**: 由 `docs/agents/command-vocabulary.md`（本 track 产出）承载该词表；此处不改历史文件。

### D-003 · R0 的完整 GOAL 全文从未落盘，效力却声明为 binding

- **Lens**: 3
- **Location**: `.planning/whole-repo-line-review/GOAL.md:10`
- **Quoted**: `- Full original GOAL text: provided by user at track start; operating envelope and finding format are binding.`
- **Defect**: 约定缺失
- **Impact**: "operating envelope and finding format are binding"，但 binding 的文本不在仓库里。审计者无法核对 R0 是否按 GOAL 执行；R1（本 track）只能靠 10 行摘要反推。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板规定"GOAL 全文必须在 Phase 0 落盘到 `.planning/<slug>/GOAL.md`"（goal-template.md 硬性清单）。本 track 自身已实践（`.planning/prompt-command-hygiene-review/GOAL.md` 为逐字存档）。

### D-004 · 七个 GOAL.md 引用仓库外的 "goal brief/§N"

- **Lens**: 3
- **Location**: `.planning/execution-data-plane-3/GOAL.md:47`（及 6 处同类）
- **Quoted**: `## Success = acceptance checklist (§26 of the goal)`
- **Defect**: 断链引用
- **同类位置**: dataset-experiment-7:14,19（"from the goal brief"）；professional-workbench-7:26；professional-workbench-ux-6:12,39（含 "See the goal brief §9"）；professional-workbench-9:10,19（"方向不变量（§7）"、"完成定义（§8）"）；professional-workbench-8:61（"See the goal contract"）；unified-help-diagnostics-6:40（"See the goal definition"）。
- **补充证据（考古子代理发现）**: D 系列（lab-*）与 R 系列的原始 brief 以未跟踪本地文件存在于主工作树 `prompts/`（`00_goal_loop_command_review.md`–`12_spectral_library_priors.md` 共 13 件 + 3 个 zip；`git ls-tree origin/master -- prompts/` 为空）——brief 层从未进版本库，换机即失。
- **Defect（同类合并）**: 断链引用 / 约定缺失
- **Impact**: 验收判据（§9/§26）躺在仓库外。第三方只能知道"有一个 §26"，无法验证完成与否——验收门失去可核查性。
- **Verified**: verified-by-file-inspection（逐处行号见 GOAL_MATRIX.csv external_goal_refs 列）
- **Recommended fix**: goal-template.md 规定：Mission/验收中的每个判据必须自带全文，禁止 §N 外引；brief 曾以本地文件存在的，全文并入 GOAL.md。

### D-005 · .planning/* 默认忽略 + 逐 track 白名单：新 track 的规划文件会被静默吞掉

- **Lens**: 5
- **Location**: `.gitignore:119-120`
- **Quoted**: `.planning/*` / `!.planning/unified-help-diagnostics-6/`
- **Defect**: 落地可行性（约定缺失：GOAL runbook 无此步骤）
- **Impact**: 新 track 不加白名单条目时，`git add -A` 不报错、不提交 planning 产物。历史后果已发生：`.planning/spatial-scientist-harness-8/GOAL.md:1` 自述 "the original planning files were never committed (gitignore whitelist omission)"。白名单现仅显式覆盖 17/36 个已跟踪 track，其余靠先于规则被跟踪而存活。
- **Verified**: verified-by-execution（本 track Phase 0 实测：`git check-ignore -v .planning/prompt-command-hygiene-review/GOAL.md` 命中 `.gitignore:119`；`git ls-files` 为 0；加白名单后恢复）
- **Recommended fix**: goal-template.md 的 PR runbook 第 0 步 = "向 .gitignore 追加本 track 白名单（lab-spec 模式）并 `git check-ignore` 自证"；本 track 已按 DECISIONS D-009 实施。

### D-006 · 12/36 个 track 目录没有 GOAL.md

- **Lens**: 1
- **Location**: `.planning/execution-concurrency-lifecycle-9/` 等 12 个目录
- **Quoted**: `ls .planning/execution-concurrency-lifecycle-9/` → `ARCHITECTURE.md BASELINE.md CAPABILITY_MATRIX.md FINAL_REPORT.md ISSUE_TRIAGE.md MILESTONES.md OVERLAP_MAP.md OWNERSHIP.md PERFORMANCE.md REVIEW_LOG.md TEST_MATRIX.md VERIFICATION.md`（有完整规划集，独缺 GOAL.md）
- **Defect**: 约定缺失
- **Impact**: 9 系列与 3 个小写文件集 track 的指令全文未落盘，约定只能考古；与 D-003/D-004 同根：goal-brief-in-session 模式。
- **Verified**: verified-by-execution（逐目录 ls，见 GOAL_MATRIX.csv GOAL_md 行）
- **Recommended fix**: 模板硬性清单"GOAL.md 必须存在且逐字落盘"；存量缺口不做补写（避免臆造历史），在报告标注。

### D-007 · /goal 没有模板与词汇表；24 个 GOAL.md 结构各异

- **Lens**: 1 / 3
- **Location**: `review/GOAL_MATRIX.csv`（全表）
- **Quoted**: 矩阵显示 24 个 track × 24 个约定维度中，除"一次性 epic + worktree + PR 形态"外，没有任何维度是 24/24 一致的；标题格式 9 种、阶段结构 6 种、语言 3 态。
- **Defect**: 约定缺失（单一事实来源缺位）
- **Impact**: 每个 /goal 都从头即兴；质量依赖作者记忆；审查者无验收基准。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: `docs/agents/goal-template.md`（Phase 3 交付）。

### D-008 · 分支命名三态并存，worktree 命名与 slug 脱钩

- **Lens**: 3
- **Location**: `.planning/plugin-platform-8/GOAL.md:11`
- **Quoted**: `Constraints honored: no second registry/scheduler/runtime; ≤2 subagents (reserved for the final adversarial review); master read-only; worktree `feat/plugin-platform-8`; local evidence only.`
- **Defect**: 重复定义（冲突）/约定缺失
- **实测分布（考古子代理修正后的时代表述）**: `feat/` 15 个 = 平台 6.0–9.0 代；`zcode/` 8 个 = 早期 3/4/5 代（execution-data-plane-3、data-runtime-governance-4、algorithm-foundation-5、pi-spatial-scientist-harness-4）+ 全部最新 D/R 系列（lab-content-expansion、lab-spec-data-driven、whole-repo-line-review、本 track）；2 个未写分支名。远端现存分支几乎全为 `zcode/`（晚近 track 未删）。worktree 脱钩三例：execution-data-plane-3:3（目录/分支/worktree 三名互异）、pi-spatial-scientist-harness-4:4（`exp-rs-pi-harness-4`）、data-runtime-governance-4:5（`exp-rs-data-runtime-4`）；plugin-platform-8:11 把分支名写作 worktree（混淆）。
- **Impact**: 无法从 track 名推断分支/worktree 名；自动化脚本与人工导航都要考古。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板固化 `zcode/<slug>` + `../exp-rs-<slug>`（裁决：最新时代 + 远端现状均为 zcode/），slug 与目录名必须一致；模板已附时代注记。

### D-009 · token 预算三种口径，20/24 缺失

- **Lens**: 1
- **Location**: `.planning/cartography-platform-9/GOAL.md:8`；`.planning/scientific-processing-8/GOAL.md:20`；`.planning/whole-repo-line-review/GOAL.md:5`
- **Quoted**: `Development scale: program-level (≥ 3×10^8 tokens budget), not surface patches.` / `Workload class: 3e8+ token goal — depth, validation, and correctness first;` / `- **Budget**: 300,000,000 tokens total, phase allocation in PLAN.md.`
- **Defect**: 重复定义（冲突）
- **Impact**: 同一仓库三个数量级写法（3×10^8 / 3e8 / 300,000,000）；20 个 track 完全无预算段。阶段超支无"上报"依据。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板统一 `budget=300000000` 参数头 + 九阶段分配表 + 1.5× 上报规则。

### D-010 · 子代理上限约定漂移，一处与上限矛盾

- **Lens**: 3 / 2
- **Location**: `.planning/pi-spatial-scientist-harness-4/GOAL.md:73`
- **Quoted**: `| M8 | 6× adversarial review + docs + FINAL_REPORT + PR | — | |`
- **Defect**: 重复定义（冲突）/不可判定
- **Impact**: 该文件其余部分无 ≤2 子代理条款，M8 的 "6×" 与仓库事实标准（≤2 只读）冲突或至少不可判定（6 轮×2 只读？6 个子代理？）。
- **同类**: 14/24 有显式子代理条款（措辞 5 种）；10/24 缺失。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板固定 "Subagents: at most 2, both read-only" + 各自唯一职责 + 不得再派生。

### D-011 · 资源上限家族三种形态，一处偏离硬上限，CLAUDE.md 直接违反

- **Lens**: 3
- **Location**: `.planning/professional-workbench-9/GOAL.md:35` 与 `CLAUDE.md:7`
- **Quoted**: `执行模式：全自动；最多 2 个只读 subagent 做 adversarial review；CI 不作为完成条件，一切以本地可复现证据为准；构建并行度 ≤4（默认 2）。` / `*   **Build:** `cd build && cmake .. && make -j$(nproc)``
- **Defect**: 重复定义（冲突）/文档漂移
- **实测分布**: 完整现代形态（`CMAKE_BUILD_PARALLEL_LEVEL=2` + `CTEST_PARALLEL_LEVEL=1` + RSS>70%/load>1.5×cores 降 -j1 + 60s 日志）仅 2 处（lab-content-expansion:18、lab-spec-data-driven:18-19）；简版 -j2 若干；8 处缺失；pw-9 的 ≤4 偏离 -j2 事实标准。
- **Impact**: 新 agent 读 CLAUDE.md 会以 nproc 并行度轰炸构建机（16C/62GB 主机上有并发 epic，execution-data-plane-3:30 明言）；读 pw-9 会用 -j4。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板固化现代完整形态；Phase 6 修 CLAUDE.md:7-9（同步修 Quick Commands 的过时构建路径，见 D-020）。

### D-012 · CLAUDE.md 指向不存在的 `.agents/vendor/`

- **Lens**: 3
- **Location**: `CLAUDE.md:66`
- **Quoted**: `Additional vendor skills (see `.agents/vendor/` for provenance):`
- **Defect**: 断链引用
- **Impact**: 13 个 qt-*/frontend-design 技能的出处说明无处可寻。
- **Verified**: verified-by-execution（`ls .agents/vendor/` → No such file or directory）
- **Recommended fix**: Phase 6 把 provenance 内联进 CLAUDE.md（Qt AI skills 上游 URL 已在同行给出，缺的只是目录指针），删除 `.agents/vendor/` 引用。

### D-013 · 语言标准两处矛盾（C++17 vs C++20）

- **Lens**: 3
- **Location**: `CLAUDE.md:53` 与 `.agents/AGENTS.md:19`
- **Quoted**: `100% C++ (C++20) in `src/`.` / `- **Style Alignment**: Match existing project C++17 / Qt 6 coding conventions exactly.`
- **Defect**: 重复定义（冲突）
- **Impact**: 两个 agent 运行时各读一份、互不引用（D-023），对"按哪个标准写代码"给出相反答案；agent review 代码时可能按错误标准提意见。
- **Verified**: verified-by-file-inspection（CMakeLists.txt 的 CXX_STANDARD 为最终裁决依据——详见本条 Phase 6 修复时的核实记录）
- **Recommended fix**: Phase 6 以 CMake 为准统一两处表述，并建立互引（D-023 修复一并做）。

### D-014 · "mirrored" 声明与实际镜像方向不符

- **Lens**: 3
- **Location**: `CLAUDE.md:64`
- **Quoted**: `Project skills live under `.agents/skills/` (mirrored to `.claude/skills/` for Claude Code). Prefer those for engineering workflows (`tdd`, `implement`, `to-spec`, `code-review`, …).`
- **Defect**: 文档漂移
- **Impact**: 实测 37 个共同技能字节一致、13 个 qt-*/frontend-design 仅存在于 .claude 侧、0 个仅 .agents 侧（`diff -rq`）。"mirrored" 对 13 项不成立；两运行时可见技能集不同却无任何文件解释。开题时所称的 ask-matt 漂移已不存在（开题信息过时，见撤下记录 R-1）。
- **Verified**: verified-by-execution
- **Recommended fix**: `review/SKILL_MIRROR.md`（Phase 6）记录差异与解释；CLAUDE.md 措辞改为准确描述。

### D-015 · 不可判定措辞进入验收门

- **Lens**: 2
- **Location**: `.planning/professional-workbench-7/GOAL.md:52`
- **Quoted**: `GUI is a coherent professional entry; review P0/P1 zero, reasonable P2 fixed;`
- **Defect**: 不可判定措辞
- **同类**: `.planning/algorithm-foundation-5/GOAL.md:64` `## Completion gate (abridged)`（自我声明验收门不完整）；`.planning/data-runtime-governance-4/GOAL.md:22-23` `only when strictly necessary, minimal, documented in DOCS_LEDGER.md`（"strictly necessary/minimal" 不可判定）。
- **Impact**: "reasonable P2 fixed" 无法被第三方判定真假；"abridged" 等于宣布验收门有暗洞。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板规定验收门每条可第三方核查；P2 处置统一为 "fixed or justified"（cartography-platform-8:50 已有先例）。

### D-016 · /loop 体系零落地：无先例、无产物目录

- **Lens**: 1 / 4
- **Location**: `.agents/skills/loop-me/SKILL.md:14`
- **Quoted**: `A **workflow** is the spec of one loop, made real. You run a workflow on a loop: the loop is its running instantiation. Workflows live in `workflows/*.md` and are the source of truth.`
- **Defect**: 约定缺失 / 边界不清
- **Impact**: `workflows/` 目录不存在（ls 实测）；`.planning/`、`.agents/`、docs/ 中 /loop 零引用。loop-me 定义的 Trigger/Checkpoint/Push right/Brief 词汇没有任何仓库级落点；"什么时候该用 /loop 而不是 /goal" 无判据。
- **Verified**: verified-by-execution（`ls workflows/` → No such file or directory；全仓 grep）
- **Recommended fix**: `docs/agents/loop-template.md` + `docs/agents/command-vocabulary.md`（Phase 4 交付）；明确标注其为推导的新约定（DECISIONS D-007）。

### D-017 · 规划文件词汇三代并存，无规范

- **Lens**: 1
- **Location**: 各 track 目录 ls（GOAL_MATRIX.csv planning_file_set 列）
- **Quoted**: 第一代 `ARCHITECTURE BASELINE FINAL_REPORT GOAL MILESTONES PLAN REVIEW_LOG TEST_MATRIX`（如 execution-plane-runtime-7）；第二代 `+ CAPABILITY_MATRIX OWNERSHIP PERFORMANCE DOCS_LEDGER ISSUE_TRIAGE OVERLAP_MAP`（如 cartography-platform-9）；第三代 `+ DECISIONS EVIDENCE PR_BODY`（如 lab-spec-data-driven、whole-repo-line-review）；异类 `task_plan.md progress.md findings.md decisions.md` 小写集（agent-rs-performance 等 3 个）。
- **Defect**: 约定缺失
- **Impact**: 新 track 不知道该建哪些文件；审查者不知道缺哪个文件算缺陷。
- **Verified**: verified-by-execution
- **Recommended fix**: goal-template.md 的 "Required planning files" 一节固化第三代集合（GOAL/PLAN/BASELINE/DECISIONS/EVIDENCE/REVIEW_LOG/PR_BODY + 按需）。

### D-018 · 10/24 个 GOAL.md 无 PR 出口条款

- **Lens**: 1
- **Location**: cartography-platform-9、dataset-experiment-7、execution-plane-8、execution-plane-runtime-7、geospatial-data-fabric-8、plugin-platform-8、professional-workbench-7、scientific-processing-8、unified-help-diagnostics-6、verification-platform-8 的 GOAL.md（证据行见 GOAL_MATRIX.csv pr_exit 列）
- **Quoted**: 以 verification-platform-8 为例：全文 30 行无 PR/mode 术语（`.planning/verification-platform-8/GOAL.md:1-30`）
- **Defect**: 约定缺失
- **Impact**: "终态是 PR" 是 /goal 的定义性特征，1/3 的 track 未写明——agent 可能在 PR 前后自行其是（如自行合并）。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 模板固化 Exit 段："PR created, not merged; worktree retained until merge, then removed."

### D-019 · 24/24 个 GOAL.md 零技能引用：命令体系与技能体系脱节

- **Lens**: 1 / 4
- **Location**: GOAL_MATRIX.csv skills_referenced 列（全列 none）
- **Quoted**: 以篇幅最长的 pi-spatial-scientist-harness-4 为例：83 行无一处引用 `.agents/skills/`（`.planning/pi-spatial-scientist-harness-4/GOAL.md:1-83`）
- **Defect**: 约定缺失 / 边界不清
- **Impact**: 仓库同时维护 37 个技能与 24 个 GOAL.md，两套体系互不引用：冲突时（如技能说 grilling 要问用户、GOAL 说 autonomy=full 禁止提问）无裁决规则；技能的针对性能力（resolving-merge-conflicts、git-guardrails）在最容易需要的时刻（PR runbook）从未被调用。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: goal-template.md 设 "Skills" 段（仅允许列 `ls` 验证存在的技能）；command-vocabulary.md 给 /goal 与 grilling（问答式）的冲突裁决：autonomy=full 时 grilling 的提问义务由 Autonomy defaults 段预答替代。

### D-020 · CLAUDE.md Quick Commands 指向不存在的构建路径与构建器

- **Lens**: 5
- **Location**: `CLAUDE.md:7-9`
- **Quoted**: `*   **Build:** `cd build && cmake .. && make -j$(nproc)`` / `*   **Clean build:** `rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)``
- **Defect**: 文档漂移
- **Impact**: 仓库实际使用 CMakePresets（`CMakePresets.json:13` binaryDir 为 `build-dev` 等 5 个 preset）+ Ninja（`build.cmd` 将 `C:\Qt\Tools\Ninja` 加入 PATH 且 `CMAKE_BUILD_PARALLEL_LEVEL=2`）。照抄 CLAUDE.md 会 (a) 在错误目录构建，(b) 用 make 而非 Ninja，(c) -j$(nproc) 违反资源上限（D-011）。
- **Verified**: verified-by-execution（CMakePresets.json、build.cmd 实测）
- **Recommended fix**: Phase 6 重写 Quick Commands 为 preset + -j2 形态。

### D-021 · wayfinder 与 /goal 的边界无处落盘

- **Lens**: 4
- **Location**: `CONTEXT.md`（全文件无 wayfinder）；`docs/agents/issue-tracker.md`（唯一正式提及）
- **Quoted**: `grep -n "wayfinder" CONTEXT.md` → 无输出
- **Defect**: 边界不清
- **Impact**: wayfinder 技能自述 "Plan a huge chunk of work (more than one agent session can hold) as a shared map of decision tickets"，与 /goal（单 track 长跑）的分工对 agent 不可见；超大工程（如 9 系列）该用哪个无判据。
- **Verified**: verified-by-execution
- **Recommended fix**: command-vocabulary.md 的判据图（Phase 4）。

### D-022 · P0–P3 严重度词汇仅存于一个 track 的 GOAL.md

- **Lens**: 3
- **Location**: `.planning/whole-repo-line-review/GOAL.md:9`
- **Quoted**: `- **Severity vocabulary** (repo-existing): P0 崩溃/内存破坏/科学结论错误/数据静默损坏/凭据泄漏; P1 未处理边界/竞态/泄漏/契约违背; P2 性能/非确定性/错误类型不一致/缺失防御; P3 代码债/文档脱节.`
- **Defect**: 重复定义风险/约定缺失
- **Impact**: 自称 "repo-existing" 却无 docs 落点（`grep -rln "P0" docs/*.md CONTEXT.md` → 空）；P0/P1=0 验收门（12 个 track 使用）引用的词表无处可查。
- **Verified**: verified-by-execution
- **Recommended fix**: command-vocabulary.md 收录 P0–P3 定义（引用 R0 原文为出处），供后续 track 复用。

### D-023 · AGENTS.md 与 CLAUDE.md 零互引，双运行时各读一份且已实际漂移

- **Lens**: 3
- **Location**: `.agents/AGENTS.md`（全文件）与 `CLAUDE.md`（全文件）
- **Quoted**: `grep -n "CLAUDE" .agents/AGENTS.md` → 无输出；`grep -n "AGENTS" CLAUDE.md` → 无输出
- **Defect**: 文档漂移
- **Impact**: zcode 读 AGENTS.md、Claude Code 读 CLAUDE.md，两份"权威"已产生 C++17/20 矛盾（D-013）与资源上限矛盾（D-011）；没有任何机制提醒维护者同步。
- **Verified**: verified-by-execution
- **Recommended fix**: Phase 6 两文件互相添加一行 pointer（按 writing-for-agents 的 context-pointer 规则措辞：一句话 + 触发条件）。

### D-024 · 最佳实践证据政策孤立存在于单个 track

- **Lens**: 1
- **Location**: `.planning/verification-platform-8/GOAL.md:24-25`
- **Quoted**: `No online CI. Every capability claim in the final report maps to a local command + exit code, or is explicitly marked not-compiled / not-executed.`
- **Defect**: 约定缺失（正向：未被推广）
- **Impact**: 这是全仓库最强的可核查性条款，但只活在一个 GOAL.md 里；其余 23 个 track 的证据标准参差。
- **Verified**: verified-by-file-inspection
- **Recommended fix**: 收进 goal-template.md 的 Evidence 段（Phase 3）。

### D-025 · /goal 开题自身含未验证定量断言（本 track 前提）

- **Lens**: 2
- **Location**: 本 track GOAL 输入（存档于 `.planning/prompt-command-hygiene-review/GOAL.md`）："30 个 track 的 GOAL.md 全部由它产出"
- **Quoted**: `.planning/ 下 30 个目录，各有 GOAL.md`
- **Defect**: 不可判定措辞（未经验证的断言）
- **Impact**: 实测 efc5c52f 处 21 个、当前 origin/master 24 个 GOAL.md（E-003）。按错误前提设计的穷尽度要求会误导执行者；这条记录同时是模板规则"Mission 中每个数字断言必须带验证命令"的反面教材。
- **Verified**: verified-by-execution
- **Recommended fix**: goal-template.md 硬性清单新增"每个现状断言附验证命令/文件:行号"。（Phase 7 已实施。）

### D-026 · unified-help-diagnostics-6 引用不存在的目标名 `sicnu_cli`

- **Lens**: 3
- **Location**: `.planning/unified-help-diagnostics-6/GOAL.md:33`
- **Quoted**: `| CLI help | `sicnu_cli` help projections |`
- **Defect**: 断链引用
- **Impact**: 按 GOAL 找 CLI help 落点的 agent 会去找 `sicnu_cli` 而失败；真实目标名是 `sicnu_geo_rs_cli`（`src/cli/CMakeLists.txt:9`）。（考古子代理发现。）
- **Verified**: verified-by-execution（子代理实测 src/cli/CMakeLists.txt）
- **Recommended fix**: 历史文件不改写；记录于此，CLI 相关 track 以 `sicnu_geo_rs_cli` 为准。

### D-027 · ADR 0144 编号被三个文件复用

- **Lens**: 3
- **Location**: `docs/adr/0144-execution-plane-8.md` / `docs/adr/0144-harness-8.md` / `docs/adr/0144-model-runtime-platform-9.md`
- **Quoted**: 三个文件共用编号 0144（`.planning/spatial-scientist-harness-8/GOAL.md:5` 引用的是其中之一："decision record `docs/adr/0144-harness-8.md`"）
- **Defect**: 重复定义（编号冲突）
- **Impact**: 按 "ADR 0144" 引用无法定位唯一文件；domain-modeling 技能的 ADR 编号约定被并发 track 打破。（考古子代理发现。）
- **Verified**: verified-by-execution（子代理实测 docs/adr/）
- **Recommended fix**: 编号重排归 domain-modeling 体系处理（本 track write scope 外，只记录）；goal-template 不受影响。

### D-028 · 根目录构建脚本硬编码已消亡的 track worktree 路径

- **Lens**: 5
- **Location**: `build.cmd:4`
- **Quoted**: `cd /d C:\Users\wangj.KEVIN\projects\exp-rs-unified-help-diagnostics-6`
- **Defect**: 文档漂移（脚本与仓库状态脱节）
- **Impact**: 按脚本名字面用途执行会把构建引到已不存在的目录；`configure_wb7.cmd` / `build_wb7.cmd` 同类。脚本实为历史 track 的本机一次性产物，却躺在仓库根。
- **Verified**: verified-by-execution（子代理实测目录不存在；本 track 主代理复核 build.cmd:4 原文）
- **Recommended fix**: 删除或移入 `.planning/` 对应 track（本 track write scope 外，只记录）；goal-template 已加"Do not follow build.cmd"防线。

---

## Part II · 事实标准约定归纳（de facto conventions）

依据 GOAL_MATRIX.csv（24 track × 24 维度）。分级：**L1 = ≥80% 明文一致（事实标准，模板直接固化）**；L2 = 多数一致、少数漂移（模板固化 + 模板注明漂移已裁决）；L3 = 少数 track 的局部实践（模板作为可选段收录）。

| # | 约定 | 级别 | 明文覆盖率 | 证据锚点 |
| --- | --- | --- | --- | --- |
| 1 | 一次性 epic、终态 PR、不合并 | L1 | 形态 24/24；PR 出口明文 14/24 | lab-content-expansion:24 "PR created, NOT merged" |
| 2 | master 只读，worktree + branch 先行 | L1 | 明文 12/24，无违例记录 | professional-workbench-ux-6:14 |
| 3 | 无 CI：本地证据 only | L1 | 明文 17/24，表述 8 种变体 | whole-repo-line-review:6 |
| 4 | 子代理 ≤2、只读、对抗审查用 | L1 | 明文 14/24 | cartography-platform-8:11 |
| 5 | 构建并行度 -j2 家族 + 禁 nproc | L2 | 完整形态 2、简版 13、缺失 8、偏离 1 | lab-spec-data-driven:18-19 |
| 6 | FAIL-honesty：FAIL 不得包装成 success | L2 | 明文 12/24，3 种表述 | dataset-experiment-7:28 |
| 7 | Review gate：P0/P1=0；P2/P3 fixed or justified | L2 | 明文 12/24 | cartography-platform-8:49-50 |
| 8 | 基线 SHA 钉定 + 复验日期 | L2 | 18/24 | geospatial-data-fabric-8:5-6 |
| 9 | 规划文件族（BASELINE/REVIEW_LOG/…） | L2 | 三代词汇并存 | D-017 |
| 10 | 分支 `zcode/<slug>` | L2 | 7 zcode/ vs 14 feat/，新近 track 全部 zcode/ | D-008 |
| 11 | token 预算 300M + 阶段分配 | L3 | 3/24（口径不一） | D-009 |
| 12 | Autonomy defaults 段 | L3 | 2/24 完整 | lab-spec-data-driven:23-34 |
| 13 | offscreen Qt 测试 + targeted ctest -R -j1 | L3 | 5/24 | lab-content-expansion:19 |
| 14 | 60s CPU/RSS 资源日志 | L3 | 2/24 | lab-spec-data-driven:19 |
| 15 | 证据政策（claim → command+exit code） | L3 | 1/24（最佳实践） | verification-platform-8:24-25 |
| 16 | 架构不变量段（single chain/agent loop/renderer） | L2 | 13/24 | execution-plane-8:9-12 |
| 17 | GOAL 全文落盘 | L3 | 4/24 明示（含 R0 反例） | D-003/D-006 |
| 18 | .planning 白名单自检 | L3 | 0/24 明示（事故 1 起） | D-005 |

裁决规则（Autonomy default #2 的应用）：实践中被执行的约定 > .planning/*/GOAL.md > .agents/AGENTS.md > CLAUDE.md > README。以上 L1/L2 归纳即"实践被执行"层；模板逐条与之对齐，冲突处（如 pw-9 的 ≤4）按多数+新近裁决为 -j2。

---

## Part III · 撤下记录（假阳性率分母）

| ID | 候选缺陷 | 撤下理由 |
| --- | --- | --- |
| R-1 | "`.agents/skills/` 有 teach、to-questionnaire 而 `.claude/skills/` 有 ask-matt、setup-matt-pocock-skills"（开题断言的技能漂移） | 实测两侧 37 项共同技能字节一致，ask-matt/setup-matt-pocock-skills 双侧均存在——漂移已在此前被修复。开题信息过时，不构成当前缺陷（verified-by-execution：diff -rq 0 处 differ）。 |
| R-2 | "cartography-platform-7 缺 no-CI 条款"（独立缺陷） | 并入 D-007 的覆盖率证据：单 track 缺单一条款是"无模板"的实例而非独立缺陷，单独立条会重复计数。 |
| R-3 | "matt / planningwithfiles / gstack 是仓库内断链引用" | 全仓 grep 无任何仓库文件引用 planningwithfiles/gstack 作为技能（CHANGELOG.md:1017 记录的是"安装"动作本身，是历史事实陈述而非断链）；gstack 相关路径引用仅 docs/agent/progress.md:1382（历史工作记录）。定性改为"外部技能未落盘"（SKILL_INVENTORY.md），不进缺陷清单。 |
| R-4 | "loop-me 的 disable-model-invocation: true 与 GOAL 用法矛盾" | disable-model-invocation 只限制模型自动触发，不限制用户显式调用 /loop-me；无矛盾。 |

**假阳性率**：提交 28 条（D-001–D-025 主审 + D-026–D-028 交叉复核新增），撤下 4 条；撤下数/提交数 = 4/28 ≈ 14.3%（口径与计算见 Phase 8 执行摘要）。
