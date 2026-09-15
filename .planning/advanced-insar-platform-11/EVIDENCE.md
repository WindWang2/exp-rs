# EVIDENCE — 本地可复现证据日志（Local evidence only; no online CI dependency）

格式：日期 | 命令 | exit | 关键输出 | 结论。所有 claim 必须可映射到本文件某行。

## Phase 0

- 2026-09-15 | `git fetch origin --prune && git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` | 基线 SHA
- 2026-09-15 | `gh pr list --state open` | 0 | 1 open：#1008 spectral（CONFLICTING） | 并发面
- 2026-09-15 | `gh pr diff 1008 --name-only` | 0 | 39 files，SAR 主写域零交集 | PARALLEL_OWNERSHIP
- 2026-09-15 | `gh issue list --state open` | 0 | #1001–#1007，无 SAR 条目 | OUT_OF_SCOPE dedupe
- 2026-09-15 | `git worktree add ../exp-rs-advanced-insar-platform-11 -b zcode/advanced-insar-platform-11 origin/master` | 0 | HEAD a5b11b7f | worktree 建立
- 宿主资源观测方式：Windows/Git Bash；RSS 经 `tasklist`（或 PowerShell Get-Process）；
  负载平均在 Git Bash 下不可测（uptime 无意义）→ 按约定记录一次 not-executed，
  构建恒定 `-j2` 上限。

## OUT_OF_SCOPE

- #1001/#1002/#1003/#1004/#1005/#1006/#1007（io/workflow/dataset/georef/agent 领域缺陷，
  非本 track 主写域；未修复，留原 track）。
- ISSUES.md S-1/S-2（多时相变化日历语义 / 极化分解）——非 InSAR mission，不在本 track。

## not-executed

-（暂无；随阶段回填）

## 测试证据

-（随 Phase 回填：命令、exit、测试计数）
