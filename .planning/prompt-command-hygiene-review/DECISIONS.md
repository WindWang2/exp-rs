# DECISIONS — prompt-command-hygiene-review

按 GOAL 的 Autonomy defaults 记录本 track 做出的每一个预设决策。格式：决策 → 选项 → 采纳 → 理由。

## D-001 · worktree 基线点

- 选项：(a) 按 GOAL 头部钉死 `origin/master @ efc5c52f`；(b) 按 PR runbook 第 1 条用 `origin/master` 最新 tip。
- 采纳：(b) `60179408`（2026-09-13 fetch 后的 origin/master）。
- 理由：用户指令"拉取最新代码"在前；runbook 第 1 条写的是 `origin/master` 而非 SHA；track 还要求每个 checkpoint rebase origin/master，晚切不如早切。GOAL 头部的 `efc5c52f` 是写作时点快照，已过时（此后 master 新合并多个 track）。

## D-002 · "30 个 GOAL.md" 前提偏差的处置

- 事实：efc5c52f 处 `.planning/` 有 33 个 track 目录、21 个 GOAL.md；当前 origin/master 有 36 个历史 track 目录、24 个 GOAL.md。
- 采纳：穷尽覆盖全部 24 个实际存在的 GOAL.md（逐个读取），并把 12 个无 GOAL.md 的 track 目录作为一等发现进入矩阵与缺陷清单；不凑数、不虚构。
- 理由：Completion gate 的本意是"穷尽、无抽样"；数字前提本身未经验证，如实修正并记录（进入 PROMPT_DEFECTS.md 作为 GOAL 写作规范的教训：现状断言必须先验证）。

## D-003 · 主工作树中已存在的三份未跟踪草稿

- 事实：主工作树（非本 worktree）存在未跟踪的 `docs/agents/goal-template.md`（183 行）、`loop-template.md`（183 行）、`command-vocabulary.md`（106 行），内容与本 track 交付物高度同源，疑为早前会话产物。
- 采纳：本 worktree 内交付物全部从本 track 自行验证的证据重新撰写；旧草稿仅作对照参考，其中每一条事实断言（如"30 个 track"）都重新核实后才可采用。
- 理由：草稿无法追溯证据；本 track 的核心主张就是"约定必须有 文件:行号 证据"。同时它们未被 git 跟踪、不在 PR diff 内，不构成冲突。

## D-004 · AGENTS.md:3 断链的修复方式

- 选项：(a) 重建 `.agents/skills/karpathy-guidelines/` 技能；(b) 把断链指针删除，使 AGENTS.md 自包含。
- 采纳：(b) 删除死指针，AGENTS.md 自包含；跨文件协作约定改为指向真实存在的 `.agents/skills/writing-for-agents/`。
- 理由：仓库内不存在"Karpathy Guidelines"的任何可考文本（无目录、无 ADR、无 vendor 副本）；重建技能等于编造出处，违反"不臆造历史"。四条原则本就完整内联在 AGENTS.md 里，指针只是失效的出处引用。原始出处标记 UNDETERMINED（见 PROMPT_DEFECTS.md D-001）。

## D-005 · karpathy 出处标注

- 采纳：AGENTS.md 保留四条原则的实质内容，不再声称"derived from the Karpathy Guidelines"；对"该出处原本指什么"标 UNDETERMINED，列为本 track 待澄清项。
- 理由：同 D-004。不臆造历史优先于保留体面感。

## D-006 · 子代理启用范围

- 采纳：仅 Phase 5 启用两个只读子代理（A 考古对照、B 对抗测试），均不得派生子代理；其余阶段主代理独力完成全部阅读与判断。
- 理由：GOAL 明文规定。本记录确认执行。

## D-007 · /loop 模板的"先例真空"标注

- 事实：`/loop` 在 `.planning/`、`.agents/` 中零引用；loop-me 技能是 grilling 型规格化会话（产出 `workflows/*.md`），不是运行时循环引擎。
- 采纳：loop-template.md 的头部注明"本仓库尚无 /loop 实战先例；本模板是由 loop-me 词汇 + 仓库硬约束推导的新约定"，不伪装成已验证约定。
- 理由：与 /goal（24 个实战样本）的证据等级不同，必须显式区分，否则模板使用者会误把新约定当事实标准。

## D-008 · 无 CI 的含义

- 采纳：全程不触发、不等待、不引用 GitHub Actions；验证全部以本地命令 + 输出进入 EVIDENCE.md。
- 理由：GOAL 明文规定。本 track 未执行任何编译（纯文档审查），无资源日志需求；若后续触发验证性编译，按 -j2 上限并记录。

## D-009 · .gitignore 增加 `.planning/prompt-command-hygiene-review/` 白名单（机械必需）

- 事实：`.gitignore:119` 为 `.planning/*` 全忽略 + 逐 track 白名单（仅 17 个 track 有显式条目）。新 track 不加白名单时，`git add -A` 会静默吞掉其全部 planning 产物——`git check-ignore -v` 实测本 track 的 GOAL.md 被忽略、`git ls-files` 为 0。历史后果已发生：spatial-scientist-harness-8 的原规划文件即因"gitignore whitelist omission"丢失（其 GOAL.md:1 自述）。
- 采纳：按 lab-spec-data-driven 模式（GOAL.md:126-127 先例）加入三行：目录白名单 + 全忽略 + `*.md` 再白名单（markdown 落盘、日志本地）。
- 理由：Completion gate 要求 `.planning/prompt-command-hygiene-review/EVIDENCE.md` 成为交付物，无白名单则物理不可能。`.gitignore` 不在 write scope 清单内，但这是 track 自身出口的机械必需，同 lab-spec-data-driven GOAL.md:45-46"机械必需行"先例。此机制本身记入 PROMPT_DEFECTS.md（D-005）。
