# EVIDENCE — F15 mosaic-fusion-11

## 环境与 configure

- 主机：16 cores / 64 GB RAM / Linux 6.18.50-2-lts；磁盘可用 157G。
- Worktree：`../exp-rs-mosaic-fusion-11` @ `a5b11b7f10`，分支 `zcode/mosaic-fusion-11`。
- Configure：`cmake --preset dev-default`（后台启动于 Phase 0）→ `build-dev/configure.log` 记录 exit。
- 资源上限执行：`CMAKE_BUILD_PARALLEL_LEVEL=2`，build `-j2`，test `-j1`，`QT_QPA_PLATFORM=offscreen`。
- 编译期 60s CPU/RSS/负载采样：见 PERFORMANCE.md。

## 验证记录

（每条 gate：命令 → exit → 关键输出摘要；追加式记录）

| Phase | 命令 | exit | 摘要 |
|---|---|---|---|
| 0 | `git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` |
| 0 | `git worktree add ...` | 0 | 分支创建成功 |
| 0 | `git check-ignore -v .planning/mosaic-fusion-11/GOAL.md` | 0 | `.gitignore:119:.planning/*`（按 #1008 先例 `git add -f` 跟踪） |

## OUT_OF_SCOPE

- Issues #1001–#1007（dataset/workflow/georef/io 域 fail-open/fail-closed 缺口）：与本 track 无文件交集，不修。
- PR #1008 与 master 的 merge conflict（DIRTY）：其 track 所有者事务。
- `mosaic_dialog`/`fusion_dialog` GUI 未接入新算子（D-011，follow-up）。

## not-executed

（如有：列出条目 + 不可自动化的原因）

## 预算记录

（Phase 超出 1.5× 时记录：phase、耗时、命令、touch 文件）
