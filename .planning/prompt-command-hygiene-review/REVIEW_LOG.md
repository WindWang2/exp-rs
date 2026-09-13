# REVIEW_LOG — prompt-command-hygiene-review

执行日志与裁决记录。每 Phase 一节；Phase 5 记录子代理对照与对抗测试结果；Phase 7 记录 UNDETERMINED 裁决。

## Phase 0 · 技能真实清单 + 去重基线（2026-09-13）

已执行：
- `git fetch origin`：origin/master `27b9aa0a..60179408`（efc5c52f 已被超越；采纳 D-001）。
- worktree 建于 `../exp-rs-prompt-command-hygiene-review`，分支 `zcode/prompt-command-hygiene-review` @ 60179408。
- `.agents/skills/`：37 个技能，`karpathy-guidelines` 不存在（`ls` → No such file or directory）。
- `.claude/skills/`：50 个技能；`diff -rq` 两侧 → 13 行全部为 "Only in .claude/skills/"（12 个 qt-* + frontend-design），0 处内容差异；`.agents` 独有 0 个。两侧均被 git 跟踪（37 / 50）。
- `gstack`、`planning-with-files` 不在仓库，但存在于用户级运行时 `~/.zcode/skills/`（对"本机 runtime"可用、对"仓库"缺失——两类引用需区分）。
- `ask-matt` + `setup-matt-pocock-skills` 双侧均存在（GOAL 开题时所称的漂移已被修复）；"matt" = Matt Pocock 技能集的路由技能（ask-matt 自述 "A router over the skills in this repo"）。
- 去重基线：AUDIT_DOSSIER_ISSUES_747_760.md 与 PROJECT_REVIEW_DOSSIER_5.0.md grep 检查 → 全部为 src 级代码发现，无命令体系/文档类条目。overlaps: 无。
- 关键新发现（开题未列）：CLAUDE.md:7 `make -j$(nproc)` 与 -j2 硬上限冲突；CLAUDE.md:53 "C++20" vs .agents/AGENTS.md:19 "C++17"；CLAUDE.md:66 引用的 `.agents/vendor/` 不存在；CLAUDE.md:7/9 Quick Commands 与本仓库 CMake preset 体系的关系未说明。

## Phase 1 · 24 个 GOAL.md 穷尽读取（2026-09-13）

- 24 个 GOAL.md 全部逐个 `cat -n` 读取（非抽样）；12 个无 GOAL.md 的 track 目录逐个 `ls` 取证。
- 产出 `review/GOAL_MATRIX.csv`（track × convention × value × evidence）。
- 结构性发现：track 规划文件存在三代词汇（详见 SKILL_INVENTORY/GOAL_MATRIX）。
- `spatial-scientist-harness-8/GOAL.md:1` 自述为重建件（原规划文件因 gitignore 白名单遗漏丢失）→ 规划文件丢失风险进入缺陷清单。
- `whole-repo-line-review/GOAL.md:10` 自述"Full original GOAL text: provided by user at track start"从未落盘 → 单一事实来源缺失的最强实例。

## Phase 2 · 缺陷清单（五透镜）（2026-09-13）

- 产出 `review/PROMPT_DEFECTS.md`；每条含 Lens/Location/Quoted/Defect/Impact/Verified/Recommended fix。
- 引不出来的候选缺陷已删除（开题清单中一条被撤下，见该文件末尾的撤下记录）。

## Phase 3 · /goal 模板（2026-09-13）

- 产出 `docs/agents/goal-template.md`：事实标准约定固化 + 参数头 + 九阶段预算范式 + 硬性自查清单。

## Phase 4 · /loop 模板 + 词汇表（2026-09-13）

- 产出 `docs/agents/loop-template.md`（Trigger/Checkpoint/Push right/Brief + 仓库硬约束注入；标注无先例，见 D-007）。
- 产出 `docs/agents/command-vocabulary.md`（/goal vs /loop vs wayfinder vs grilling 判据图 + 3 个真实历史 track 归类示例）。

## Phase 5 · 交叉复核（2026-09-13）

（待填：subagent A 对照结果、subagent B 漏洞清单、逐条裁决。）

## Phase 6 · AGENTS.md 修复 + 镜像记录（2026-09-13）

（待填。）

## Phase 7 · 模板修订 + UNDETERMINED 裁决（2026-09-13）

（待填。）

## Phase 8 · 执行摘要 + PR（2026-09-13）

（待填。）
