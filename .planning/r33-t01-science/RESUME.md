# R33-T01 RESUME

## 固定变量（新 shell 必须先恢复）

```bash
EXPRS_TRACK=t01-science
EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r33-t01-science-20261117T150207Z
EXPRS_BRANCH=zcode/r33-t01-science-20261117T150207Z
EXPRS_BASE_BRANCH=master
EXPRS_BASE_SHA=56f0bd2a0a6a05827c8a40770924b445a0f99862
EXPRS_BUILD=$EXPRS_WT/build-dev
EXPRS_CONFIG=Debug
EXPRS_PLAN=$EXPRS_WT/.planning/r33-t01-science
EXPRS_HOST_LOCK="$HOME/.cache/exp-rs-agent/host-build"
cd "$EXPRS_WT"
```

## 状态快照（2026-11-17T15:02Z）

- master 前进 5ff1540004 → 56f0bd2a0a：#2134/#2135/#2136/#2137 已全部合并（14:16–14:17 UTC）。
- 当前无开放 PR；#2138 关闭未合并。研究快照中 #2135 的排他路径已解除（合入 master），不重复实现其修复。
- 106 open issues（100+6 两页）。本轨候选：#2127（WP01 优先）、#2126、#2128、#2102、#2129、#2091、#2094、#2095、#2088、#2100、#2092、#2093、#2096、#2097、#2101、#2105、#2103。
- 本地另有 r33-t06 worktree（另一轨），未见其远端分支；不改别人分支。

## 恢复步骤

1. 恢复上述变量，cd 到 EXPRS_WT。
2. 读本目录 LEDGER.md / PLAN.md / DECISIONS.md / REVIEW_R1.md / REVIEW_R2.md。
3. `git status --short` 确认干净；构建用双锁（host lock + build-dir lock），`-j4` 上限。
4. 未完成工作包按 PLAN.md 优先级继续。
