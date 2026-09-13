# GOAL — R1 · Prompt & Command Hygiene Review（/goal 与 /loop 指令体系审查与优化）

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best
mode=review-only  write-access=docs+review+commands  src-access=read-only

  Track branch: zcode/prompt-command-hygiene-review (worktree ../exp-rs-prompt-command-hygiene-review, off origin/master @ efc5c52f)
Mode: unattended long-running epic. 纯本地审查 + 文档产出；不触发、不等待、不引用任何远端 CI。
Write scope: review/、docs/agents/、.agents/skills/、AGENTS.md、CONTEXT.md、CLAUDE.md。
src/ 与 tests/ 只读。 本 track 不改任何产品代码。

> 本文件是 track 启动时用户提供的原始 GOAL 指令的逐字存档（源：会话输入；同文本曾以未跟踪文件
> `prompts/00_goal_loop_command_review.md` 存在于主工作树）。按本 track 自己的规范，GOAL 全文
> 必须落盘——这正是 R0（whole-repo-line-review）缺失的东西（见其 GOAL.md:10 的自述）。

## Mission

这个仓库的开发效能建立在两套指令体系上，而不是产品代码上：

- /goal —— 长跑 epic 驱动。.planning/ 下 30 个 track 的 GOAL.md 全部由它产出，
分支命名 zcode/<track>、worktree ../exp-rs-<track>、≤2 只读子代理、-j2 硬上限、
300M token 预算、无 CI、gh pr create 不自动合并——这套约定已经在多轮实战里被验证有效。
- /loop —— 常驻循环驱动。loop-me 技能把它定义为"把用户生活/工作里可委派的复现模式
规格化"：Trigger（事件优于排程）、Checkpoint（push right：把人工介入推到最晚、一次问清）、
Brief（决策就绪的摘要，不是原始产出）。

但这两套体系本身从来没有被审查过。 三个问题已经可以被证实：

1. AGENTS.md 引用了一个不存在的技能。 第 3 行写着"derived from the
Karpathy Guidelines"，
但 .agents/skills/ 下没有 karpathy-guidelines 目录。这是全仓库 agent 行为准则的
唯一权威来源，而它的来源链接是断的。
2. /goal 提示词没有单一事实来源。 .planning/ 里 30 个 track 的 GOAL.md 结构各异，
却没有任何模板；ISSUE_TRIAGE.md 里提到"Classification per /goal vocabulary"，
但那份 vocabulary 没有落盘。约定散落在历史文件里，靠人读出来。
3. 技能清单与文档脱节。 CLAUDE.md 说"Project skills live under .agents/skills/
(mirrored to .claude/skills/)",但两侧已经漂移（.agents/skills/ 有 teach、
.claude/skills/ 有 ask-matt）。同时 planningwithfiles、gstack、matt 这三个
被引用的技能在仓库里根本不存在。
本 track 的产出：一份能直接落地的指令体系规范 + 一组优化后的 /goal 与 /loop 模板，
让下一位 agent（或人）不必再靠考古来复现约定。

（原文含 Operating envelope、Review lenses、Work packages A–H、Execution order 0–8、
Finding format、Required artifacts、Completion gate、PR runbook；全文见会话存档与
REVIEW_LOG.md 的执行记录。以下为逐字保留的关键约束摘要。）

## Operating envelope (non-negotiable)

- Autonomy: fully unattended. No clarifying questions, no option menus. Take the default in
Autonomy defaults; record in .planning/prompt-command-hygiene-review/DECISIONS.md.
- Agent: zcode. Token budget: 300,000,000（allocation below）。Report — never silently
stall — if a phase exceeds 1.5× its budget.
- Subagents: at most 2, both read-only，仅在 Phase 5 启用（subagent A 考古对照、
subagent B 对抗测试；不得再派生子代理）。
- No CI: never wait on, trigger, or cite GitHub Actions. 本地证据 only → EVIDENCE.md。
- Branching: master read-only. Create worktree + branch before the first edit.
- Build resources (hard): CMAKE_BUILD_PARALLEL_LEVEL=2, CTEST_PARALLEL_LEVEL=1; Ninja -j2,
-j1 when RSS > 70% or load > 1.5× cores; never -j$(nproc)。纯文档审查正常路径不编译。
- Exit: PR created; worktree retained until merge, then removed.

## Required artifacts

- review/SKILL_INVENTORY.md
- review/GOAL_MATRIX.csv
- review/PROMPT_DEFECTS.md
- review/SKILL_MIRROR.md
- docs/agents/goal-template.md
- docs/agents/loop-template.md
- docs/agents/command-vocabulary.md
- .agents/AGENTS.md（修复后）
- .planning/prompt-command-hygiene-review/EVIDENCE.md

## PR runbook (execute verbatim)

1. git worktree add ../exp-rs-prompt-command-hygiene-review -b zcode/prompt-command-hygiene-review origin/master
2. 每个 Phase 完成后提交一次，commit message 前缀 review(commands): 或 docs(agents):。
3. 每个 checkpoint 执行 git fetch origin && git rebase origin/master。
4. 若需验证技能流程：只编译相关 target，-j2，QT_QPA_PLATFORM=offscreen，ctest -R <name> -j1。
5. 最后提交前执行模板引用的存在性断言，结果粘进 EVIDENCE.md。
6. git push -u origin zcode/prompt-command-hygiene-review — 若被护栏 hook 拦截，重试一次，
记入 PR_BODY.md 后继续；绝不 force。
7. gh pr create --base master --head zcode/prompt-command-hygiene-review
   --title "docs(agents): /goal and /loop command system review, templates and vocabulary"
   --body-file .planning/prompt-command-hygiene-review/PR_BODY.md
8. 不合并、不建远端 issue、不等 CI。 报告 PR URL 后停止。
