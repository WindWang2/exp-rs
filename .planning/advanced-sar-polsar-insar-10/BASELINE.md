# BASELINE — advanced-sar-polsar-insar-10

- **Track**: `zcode/advanced-sar-polsar-insar-10`
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-advanced-sar-polsar-insar-10`
- **Baseline SHA**: `7d78059d1a6d316d606656759a506d17bc5e3b55` (= origin/master @ track start, 2026-09-13)
- **Verify**: `git -C <worktree> rev-parse origin/master` → same SHA
- **Worktree created from**: `git worktree add ../exp-rs-advanced-sar-polsar-insar-10 -b zcode/advanced-sar-polsar-insar-10 origin/master`

## 同名分支/worktree 排查

- `git branch -a | grep -i sar` → 无 SAR 相关分支（本地/远端）。
- `git worktree list` → 14 个 worktree，无 SAR 命名。
- 结论：无历史残留冲突，分支名 `zcode/advanced-sar-polsar-insar-10` 全新。

## 本地环境备注

- `.git/info/exclude`（本地文件，非 tracked）原含 `.planning/` 目录级排除，会阻断
  `.gitignore` whitelist 模式（目录被排除后 negation 无法生效）。已改为 `.planning/*`
  （与 tracked `.gitignore` 自身的 `.planning/*` 模式一致，保持对未白名单目录的排除语义）。
  `git check-ignore -v .planning/advanced-sar-polsar-insar-10/GOAL.md` 现命中 negation
  规则，`git status` 显示 untracked —— 文件可被跟踪。
- 编译配置：`cmake --preset build-dev`；host = linux 6.18，16 核 / 62 GB。
