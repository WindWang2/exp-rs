# R30-T05 RESUME — 会话恢复入口

每次新 shell 先读本文件重建变量。七会话共享 host-build 锁。

## 固定路径

- EXPRS_REPO（主检出，只读参考）: `/home/kevin/projects/rs-studio/main`
- 本轨 worktree: `/home/kevin/projects/rs-studio/exp-rs-r30-t05-pi-mcp-session-20261113T174105Z`
- 本轨分支: `zcode/r30-t05-pi-mcp-session-20261113T174105Z`
- EXPRS_BASE_SHA: `606e3c91017d8dc3d3ad363e613065ef214a82c6`（origin/master @ 2026-11-14 fetch）
- EXPRS_BUILD: `$EXPRS_WT/build-dev`
- EXPRS_PLAN: `$EXPRS_WT/.planning/r30-t05-pi-mcp-session`
- EXPRS_HOST_LOCK: `${XDG_CACHE_HOME:-$HOME/.cache}/exp-rs-agent/host-build`（canonical 绝对路径，七轨一致）
- EXPRS_TRACK: `t05-pi-mcp-session`

## Shell 入口模板

```bash
EXPRS_WT="/home/kevin/projects/rs-studio/exp-rs-r30-t05-pi-mcp-session-20261113T174105Z"
EXPRS_BUILD="$EXPRS_WT/build-dev"
EXPRS_PLAN="$EXPRS_WT/.planning/r30-t05-pi-mcp-session"
EXPRS_HOST_LOCK="${XDG_CACHE_HOME:-$HOME/.cache}/exp-rs-agent/host-build"
cd "$EXPRS_WT"
```

## 当前状态（最近更新在顶部）

- 2026-11-14（深夜会话）：WP01-WP10 全部落地。
  - commits：fa24134daa（Pi WP02-04）、eeb866cf5c（journal WP06）、busy-spin+evict 第三个 commit。
  - E2E：E1 ✅、E3-A/B ✅、E2 window-1 等 build7（surface host 取消契约 fixture 修复，排队等他轨 host 锁，PID 见 /tmp/r30t05_build7.log）。
  - 全 C++ 验证套件 GREEN（LEDGER.md 有完整表）；fake-server Pi 套件 83/83。
  - R1 独立 review 后台运行中（agent）。
  - 待办：build7 → E2 + r25 cancel 复验 → commit fixture+E2E → R1 findings 处置 → R2 终审 → fetch master 复核 → PR。
- 2026-11-13T17:41Z: worktree 创建，基线 606e3c9101。

