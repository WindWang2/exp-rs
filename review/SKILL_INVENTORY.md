# 技能真实清单（Skill Inventory）

Track: prompt-command-hygiene-review · Phase 0 · 2026-09-13
方法：`ls .agents/skills/`、`ls .claude/skills/`、`diff -rq`（内容级比对）、`git ls-files`（跟踪状态）、用户级目录核查。所有状态均 verified-by-execution。

## 结论速览

| 声称/疑点 | 实际状态 | 证据 |
| --- | --- | --- |
| `karpathy-guidelines` 技能 | **missing**（仓库两侧均无） | `ls .agents/skills/karpathy-guidelines/` → No such file or directory；`.agents/AGENTS.md:3` 仍以 `file:///.agents/skills/karpathy-guidelines/SKILL.md` 指向它 |
| `planningwithfiles` 技能 | **missing（仓库）**；用户级运行时存在 `planning-with-files`（带连字符） | 不在两侧技能目录；`ls ~/.zcode/skills/` 命中 |
| `gstack` 技能 | **missing（仓库）**；用户级运行时存在 | 同上 |
| `matt` 技能 | **present**：`ask-matt` + `setup-matt-pocock-skills` 双侧均存在（GOAL 开题所称的"仅 .claude 侧"已不成立） | `ls .agents/skills/` 与 `ls .claude/skills/` 均含两项；ask-matt 自述 "A router over the skills in this repo" |
| CLAUDE.md:64 "mirrored to .claude/skills/" | **present-differs**：37 个共同技能字节级一致；`.claude` 侧多 13 个供应商技能；`.agents` 独有 0 个 | `diff -rq` → 13 行全部 `Only in .claude/skills/`，0 处 `differ` |
| CLAUDE.md:66 "see `.agents/vendor/` for provenance" | **断链**：目录不存在 | `ls .agents/vendor/` → No such file or directory |

## 全量清单（.agents/skills/，37 项，双侧共有，内容一致）

| 技能 | 用途（SKILL.md description 摘译） |
| --- | --- |
| ask-matt | 技能路由器：问哪个技能/流程适合当前情境 |
| claude-handoff | 把当前会话交给新的后台代理接续 |
| code-review | 沿两轴（规范/缺陷）审查某固定点以来的变更 |
| codebase-design | 深模块设计共享词汇 |
| diagnosing-bugs | 疑难 bug 与性能回归的诊断循环 |
| domain-modeling | 领域建模：术语、CONTEXT.md、ADR |
| git-guardrails-claude-code | 配置 hook 拦截危险 git 命令（push/reset --hard 等） |
| grill-me | 用追问磨利一个计划或设计 |
| grill-with-docs | grill-me + 边问边产 ADR/术语表 |
| grilling | 树式追问：rounds/frontier/每问附推荐答案 |
| handoff | 把当前会话压缩成交接文档 |
| implement | 按规格或票面实施一段工作 |
| implement-spec | 按规格书实施 |
| improve-codebase-architecture | 扫描深化机会并以 HTML 报告呈现 |
| loop-me | 以 grilling 纪律规格化"可委派的复现模式"（产出 workflow 规格） |
| migrate-to-shoehorn | TypeScript 测试断言迁移（与本项目无关，属上游残留） |
| prototype | 一次性原型回答设计问题 |
| research | 对高可信一手来源做调查并落盘 Markdown |
| resolving-merge-conflicts | 解决进行中的 merge/rebase 冲突 |
| retro | 会话复盘 |
| scaffold-exercises | 练习题目录脚手架（上游残留） |
| setup-matt-pocock-skills | 一次性配置：issue tracker、triage 标签、领域文档布局 |
| setup-pre-commit | Husky pre-commit 配置（上游残留，本项目未用 Husky） |
| setup-ts-deep-modules | dependency-cruiser 接线（上游残留） |
| tdd | 测试驱动开发 red-green-refactor |
| teach | 教用户一个新概念 |
| to-questionnaire | 把答不了的决策变成问卷 |
| to-spec | 把当前会话综合成规格发布到 issue tracker |
| to-tickets | 把计划/规格拆成带阻塞边的 tracer-bullet 票 |
| triage | 把 issue/外部 PR 推过 triage 状态机 |
| wait-what | "刚才那条没说明白，重新讲" |
| wayfinder | 把超大工作量映射为 issue tracker 上的决策票并逐票解决 |
| wizard | 生成供人操作的交互式 bash 向导 |
| writing-beats / writing-fragments / writing-shape | 写作三段式（beat/fragment/shape） |
| writing-for-agents | 面向 agent 的文档写作规范（skills、AGENTS.md、CLAUDE.md） |

上游残留说明：`migrate-to-shoehorn`、`scaffold-exercises`、`setup-pre-commit`、`setup-ts-deep-modules` 为 Matt Pocock 技能集整体引入时带入的 TypeScript 工具链技能，与本项目（C++/Qt）无交集；未在本 track 任何模板中引用。

## 仅存在于 .claude/skills/ 的 13 项（供应商技能）

`frontend-design` + `qt-cmake-project`、`qt-cpp-docs`、`qt-cpp-review`、`qt-figma-component-generation`、`qt-figma-token-extraction`、`qt-qml`、`qt-qml-docs`、`qt-qml-profiler`、`qt-qml-review`、`qt-qml-test`、`qt-qml-test-run`、`qt-ui-design`。

- CLAUDE.md:66-69 称其为 vendor 技能并指向 `.agents/vendor/` 取出处——**该目录不存在**，provenance 断链（缺陷 D-012）。
- "mirrored" 措辞（CLAUDE.md:64）对这 13 项不成立：它们只活在 Claude Code 侧。zcode 侧运行时读取 `.agents/skills/`，因此 zcode 代理看不到 qt-* 技能——这是有意的还是漂移，仓库内无任何文件解释（SKILL_MIRROR.md 记录）。

## 仅存在于 .agents/skills/ 的 0 项

无。`.agents` 侧是 `.claude` 侧的真子集（按目录名）。

## 用户级运行时技能（不在仓库、对断链判定的影响）

`~/.zcode/skills/` 含 `gstack`、`planning-with-files` 等。**引用判定规则**：文档若引用仓库外技能而不加限定，在干净 clone / 换机器时即断链。本 track 模板一律不引用仓库不存在的技能；历史文档中对 planningwithfiles/gstack 的引用按"未落地的外部技能"记录（缺陷 D-013/D-014）。

## 与本 track 模板的关系（Completion gate 承诺）

三份模板（goal-template / loop-template / command-vocabulary）引用的技能全部来自上表"双侧共有"清单，且每一条在 Phase 8 前用 `test -e` 断言复核（EVIDENCE.md E-007）。karpathy-guidelines、planningwithfiles、gstack、migrate-to-shoehorn 等不出现在任何模板中。
