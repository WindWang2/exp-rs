# EVIDENCE — cn-eo-product-physics-11

只记录本地可复现证据（命令 + exit + 关键输出摘要）。禁止引用在线 CI。

## Phase 0

- `git fetch origin --prune` → ok
- `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
- `gh pr list --state open` → 仅 #1008（CONFLICTING）；#991/#992 已合入 master
- `gh issue list --state open` → #1001..#1007（dataset/workflow/georef/io:clip；与本 track 无交集）
- `git worktree add ../exp-rs-cn-eo-product-physics-11 -b zcode/cn-eo-product-physics-11 origin/master` → ok（12,362 files）
- 代码审计：见 BASELINE.md / CURRENT_ARCHITECTURE.md（文件:行号 证据在文中）
- 宿主资源测量方式：Windows `tasklist`（RSS）；Git Bash 下无 load average → 按协议记录一次 not-executed，`-j2` 恒定上限。

## 构建资源记录（滚动）

（每 Phase 构建时补充：命令、-j 级别、时长、RSS 观测）

## OUT_OF_SCOPE

（Phase 2+ 发现时补充）
