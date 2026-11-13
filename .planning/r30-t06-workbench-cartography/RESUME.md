# R30-T06 · RESUME（上下文恢复入口）

> 每次恢复先读本文件，再读 `LEDGER.md` / `ACCEPTANCE.md` / `DECISIONS.md`。
> 工具调用可能使用新 shell，不假设上一个调用的变量仍存在。

## 固定常量（每个新 shell 入口显式重建）

```bash
EXPRS_TRACK="t06-workbench-cartography"
EXPRS_REPO="/home/kevin/projects/rs-studio/main"
EXPRS_STAMP="20261113T142904Z"
EXPRS_BRANCH="zcode/r30-t06-workbench-cartography-20261113T142904Z"
EXPRS_BASE_SHA="dcda90bf00a705f00be1e119e44cf1a502fd38ea"
EXPRS_WT="/home/kevin/projects/rs-studio/exp-rs-r30-t06-workbench-cartography-20261113T142904Z"
EXPRS_BUILD="$EXPRS_WT/build-dev"
EXPRS_PLAN="$EXPRS_WT/.planning/r30-t06-workbench-cartography"
EXPRS_HOST_LOCK="/home/kevin/.cache/exp-rs-agent/host-build"
```

`EXPRS_HOST_LOCK` 是 **canonical 绝对路径**（`${XDG_CACHE_HOME:-$HOME/.cache}/exp-rs-agent/host-build`，
本会话 `XDG_CACHE_HOME` 未设置 → `$HOME/.cache`）。七轨共享这一把全机锁，
不得因工作目录 / 缓存环境不同各拿一把。本轨另有自己的 build-dir 锁（`$EXPRS_BUILD`）。

## 宿主环境事实（非 ZCode）

- 当前宿主是 WorkBuddy，**没有注册 `/goal-loop`，也没有 `/goal` 命令**；仓内
  `.agents/skills/`、`.claude/skills/`、`.zcode/skills/` 均无 `goal-loop/SKILL.md`。
  按 Prompt 规定，不修改宿主 hooks/权限；采用其方法论
  （完成条件 → 聚焦改动 → 亲自验证 → 记账 → 重新判断）在本宿主自主迭代。
- 仓内确实存在并可读：`tdd`、`code-review`、`diagnosing-bugs`、`loop-me`、`implement`、
  `resolving-merge-conflicts`、`triage`（位于 `.agents/skills/`，`.claude/skills/` 为镜像）。
- `.agents/AGENTS.md` 第 7 行定义「Unattended mode（`/goal` autonomy=full）」：
  需要澄清的选项写入本轨 `DECISIONS.md` 而不打断用户。本轨按此执行。
- `gh` 2.103.3 已登录 `WindWang2`（keyring，含 `repo` scope）；`git fetch origin --prune` 成功。

## 当前状态（2026-11-13，检查点 1）

| 项 | 值 |
|---|---|
| base SHA | `dcda90bf00a705f00be1e119e44cf1a502fd38ea`（2026-11-13 20:01:29 +0800，#2044） |
| 当前 HEAD | 同 base（worktree 新建，尚无提交） |
| 默认分支 | `master`（`gh repo view` 核实） |
| open PR | **0**（PR-first 队列已清空） |
| open issues | **48**（issues API 中过滤 `pull_request != null` 后） |
| 远程分支 | 111；T06 相关 6 支均为历史 retained，非活动开发 |
| host-build 锁 | `held`（其他 agent 占用）→ 重型 configure/build 待重试，禁止删活锁 |
| 本轨 build-dir 锁 | `free` |
| 已验证能力 | `resource_guard.py --help` 可读；`--lock-dir/--parallel/--max-parallel/--wait/--status` 均支持 |
| 工具链 | CMake 4.4.5 / GCC 16.2.1 / Ninja 1.13.2 / Qt6 (`/usr/lib/cmake/Qt6`) / ccache 4.14.1（10 GB，43% 命中）/ 62 GiB RAM / 489 GiB 空闲 |
| 既有 build-dev | 仅主 worktree 有（旧 HEAD、Unix Makefiles、11-05）；**按纪律不借其二进制证明本 HEAD** |

## 下一条动作

1. 在不占锁的前提下完成 WP01 triage（已基本完成，见 `ISSUE_TRIAGE.md`），写 RED 测试与生产修复。
2. 轮询 `python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --status`；
   转 `free` 后用双锁 + 显式 `--parallel 4 --max-parallel 4` 跑 configure / build / ctest。
3. 锁忙时只做源码分析、测试编写、文档与记账；不做无信息增量的重复命令。

## 失败命令台账

| 命令 | 退出码 | 说明 |
|---|---|---|
| `ctest --test-dir <main>/build-dev -N` 合并在大批量探查里 | 137 / SIGTERM | 复合命令超时被杀；**尚未在新 worktree 重跑**，不算本轨验证 |
| host-lock `--status` | 0 | 返回 `held`，非失败 |

## 活动构建 PID / 锁

- 本轨：无（尚未启动 configure/build）。
- 全机锁：被其他 agent 持有，未知 PID；不删除、不终止。
