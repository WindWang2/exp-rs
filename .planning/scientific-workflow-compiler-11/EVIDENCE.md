# EVIDENCE — 本 track 全部本地验证证据

格式：日期 | 命令 | exit | 结果摘要。能力声明必须指向其中一行。

## Phase 0

- 2026-09-16 | `git fetch origin --prune && git rev-parse origin/master` | 0 | origin/master=a5b11b7f10fa010c1c060864fb427d777ba9a4aa（prompt 快照 ebcafb4d 已过期）
- 2026-09-16 | `gh pr list --state open` | 0 | #1009 (UNSTABLE), #1008 (CONFLICTING/DIRTY)；#991/#992 已合并
- 2026-09-16 | `gh issue list --state open` | 0 | #1001-#1007（全部范围外，见 BASELINE.md）
- 2026-09-16 | `git worktree add ../exp-rs-scientific-workflow-compiler-11 -b zcode/scientific-workflow-compiler-11 origin/master` | 0 | worktree @ a5b11b7f
- 2026-09-16 | `git check-ignore -q .planning/scientific-workflow-compiler-11/GOAL.md` | 1 | planning 目录已可跟踪（.gitignore 白名单 append-only +4 行）

### 宿主资源测量说明

- 平台 win32 / Git Bash。`uptime` load-average 在 Git Bash 不可测（not-executed，按 GOAL 允许记录一次并保持 -j2 上限）。RSS 用 `tasklist` / `powershell Get-Process` 测量；编译期间 60s 采样。

（后续 Phase 证据按序追加。）
