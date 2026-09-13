# EVIDENCE — prompt-command-hygiene-review

本地证据日志。无 CI；每条断言附命令与结果。本 track 为纯文档审查，正常路径零编译。

## E-001 · 环境与基线（Phase 0）

- `git fetch origin` → `27b9aa0a..60179408  master -> origin/master`；新增 12 个远端 track 分支。
- `git log origin/master -1 --oneline` → `60179408 Merge pull request #957 from WindWang2/zcode/whole-repo-line-review`
- `git worktree add ../exp-rs-prompt-command-hygiene-review -b zcode/prompt-command-hygiene-review origin/master` → 成功，HEAD @ 60179408。

## E-002 · 技能目录状态（Phase 0）

- `ls .agents/skills/` → 37 项；无 `karpathy-guidelines`（`ls .agents/skills/karpathy-guidelines/` → `No such file or directory`）。
- `ls .claude/skills/` → 50 项。
- `diff -rq .agents/skills/ .claude/skills/` → 13 行，全部 `Only in .claude/skills/: …`（frontend-design + 12 个 qt-*）；`grep -c differ` → 0（共同技能字节一致）。
- `git ls-files .agents/skills/ | cut -d/ -f3 | sort -u | wc -l` → 37；`.claude/skills/` → 50（两侧均被 git 跟踪）。
- `ls ~/.zcode/skills/ | grep -i "gstack\|planning"` → `gstack`、`planning-with-files`（用户级运行时存在，仓库级不存在）。
- `ls .agents/vendor/` → `No such file or directory`（CLAUDE.md:66 指向的出处目录不存在）。

## E-003 · GOAL.md 计数（Phase 1）

- `git ls-tree -r --name-only efc5c52f -- .planning/ | grep "GOAL.md$" | wc -l` → **21**（GOAL 开题声称 30）。
- `git ls-tree -d --name-only efc5c52f -- .planning/ | wc -l` → 33 目录。
- 当前 origin/master（60179408）：36 个历史 track 目录，24 个含 GOAL.md，12 个不含。
- 24 个 GOAL.md 全部逐个 `cat -n` 读取（证据见 GOAL_MATRIX.csv 逐格 文件:行号）。

## E-004 · 规划文件词汇断链（Phase 1）

- `.planning/geospatial-data-fabric-9/ISSUE_TRIAGE.md:4` = "numbers. Classification per /goal vocabulary." —— 全仓 grep `/goal vocabulary` 仅此一处，vocabulary 文件不存在。
- `.planning/spatial-scientist-harness-8/GOAL.md:1` = "Reconstructed 2026-09 from ADR 0144 + commit evidence; the original planning files were never committed (gitignore whitelist omission)."
- `.planning/whole-repo-line-review/GOAL.md:10` = "Full original GOAL text: provided by user at track start; operating envelope and finding format are binding."

## E-005 · 去重基线（Phase 0）

- `grep -n -i "AGENTS.md\|CLAUDE.md\|karpathy\|goal.md\|loop" ../exp-rs/AUDIT_DOSSIER_ISSUES_747_760.md` → 仅命中代码上下文（loop 循环、模板组件），无命令体系条目。
- 同 grep PROJECT_REVIEW_DOSSIER_5.0.md → 仅代码级发现。**overlaps: 无。**

## E-006 · 资源日志

- 本 track 无编译、无测试执行（纯文档审查 + grep/ls/git 取证）。无 CPU/RSS 日志需求。
- 若后续验证性编译：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、Ninja `-j2`、每 60s 记录 CPU/RSS（占位，未发生）。

## E-007 · 模板引用存在性断言（Phase 8 前置，runbook 第 5 条）

（待填：对三份模板中出现的每个 `.agents/skills/<name>/SKILL.md` 路径与每个被引用文件执行 `test -e` 断言，结果粘贴于此。）
