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

## 当前状态（2026-11-13，检查点 2 — 恢复时先读这里）

| 项 | 值 |
|---|---|
| 启动 base SHA | `dcda90bf00a705f00be1e119e44cf1a502fd38ea`（2026-11-13 20:01:29 +0800，#2044） |
| **当前 base SHA** | `606e3c91017d8dc3d3ad363e613065ef214a82c6`（#2053，PR 提交前 fetch 后 rebase 到此） |
| 生产 commit | `0805c5e72e`（rebase 后；原 `3d6a225d` 补丁字节等价，diff sha256 `a611a1d3…`） |
| **最终 HEAD** | `30d9afeb7f`（其后若有账本 SHA 更新提交，以 `git log -1` 为准） |
| 分支 | `zcode/r30-t06-workbench-cartography-20261113T142904Z` |
| 默认分支 | `master`；open PR 0 / open issues 48 / 远程分支 111（T06 相关 6 支 `ahead=0`） |
| 已完成 | WP01 triage、F1+F2 实现、13 个新 case、4 TU 语法自检全绿、`git diff --check` 干净、独立 R1 → fix → 独立 R2（无 P0/P1 静态阻断） |
| 阻塞 | **本地 configure/build/ctest 未完成**：CMake 生成阶段反复被宿主中断（`tests/CTestTestfile.cmake` 未生成，`build.make` 最多 2238/4133）。详见 `DECISIONS.md D20` / `LEDGER.md` 失败台账 |
| 未实跑 | `ctest -N`、`ctest -j1`、3 个既有回归、R1 P1-4 旧文案兼容性、RED |
| 本轨 build-dev | 部分生成（`CMakeCache.txt` / `Makefile` 存在，OpenCV_DIR 已写入），**不可用于证明本 HEAD**；恢复后必须重新 configure |
| 工具链 | CMake 4.4.5 / GCC 16.2.1 / Ninja 1.13.2 / Qt6 `/usr/lib/cmake/Qt6` / ccache 4.14.1（10 GB，43% 命中）/ 62 GiB RAM / 489 GiB 空闲 |
| 必需 configure 参数 | `-DOpenCV_DIR=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib/cmake/opencv5 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`（`src/processing/CMakeLists.txt:12` 硬要求；其他 R30 轨用同一前缀） |

## 恢复后的第一条动作（按序）

1. `git -C "$EXPRS_WT" status --short`、`git -C "$EXPRS_WT" log -1`；确认生产 HEAD 未漂移。
2. 重新 fetch `origin/master`：若 base 变化，更新 `EXPRS_BASE_SHA`，按 `PR_BRANCH_MATRIX.md` 复核冲突，并请 reviewer 补最终差异复核。
3. **补齐运行时验证**（本轮唯一硬缺口）：
   - 用双锁 + 显式 `--parallel 4 --max-parallel 4` 完成 `cmake --preset dev-default -DOpenCV_DIR=…`；
     若宿主仍中断生成阶段，换用非 WorkBuddy 沙箱的执行通道（其他 R30 轨的宿主可完成同样 configure）。
   - `ctest --test-dir "$EXPRS_BUILD" -N` 与 `--show-only=json-v1` 核对
     `test_r30_t06_group::` / `test_r30_t06_history::` 的真实 case 数与 prefix（**不是 0 tests**）。
   - `cmake --build "$EXPRS_BUILD" --parallel 4 --target test_r30_t06_group_zoom test_r30_t06_history_action_parity test_r29_t04_history_actions test_processing_history_model test_r22_workbench_view_tree`。
   - `ctest --test-dir "$EXPRS_BUILD" -j1 --output-on-failure -R "test_r30_t06|test_r29_t04_history|test_processing_history_model|test_r22_workbench_view_tree"`；
     重点复核 **R1 P1-4**（旧 retry 文案）。
4. 用真实输出更新 `ACCEPTANCE.md` / `LEDGER.md` / `PR_BODY.md`，把 `BLOCKED/NOT_EXECUTED` 改为 `DONE`，并把 Draft PR 转为正式 PR。
5. 再按 `PLAN.md` / `HANDOFF.md H05` 继续 WP03 / WP06–WP12，不重复本轮已完成项。

## 活动构建 PID / 锁

- 本轨：无活动构建；后台任务 `jPlsI6` / `q6817h` / `1FFxYd` / `rekU5H` / `7LKC4G` 均已结束或被停止。
- 全机锁：由其他 R30 轨竞争；期间多次出现死 PID 孤儿锁，一律用官方 `--stale-after` 规则处理，
  **未手动删活锁、未终止其他 agent、未修改 guard 默认策略**。
- 宿主 `SAFE_DELETE_BULK_CONFIRM_REQUIRED`（阈值 50）会拦截 guard 的 `.staged` 清理；
  恢复时优先用「只读 `--json --status` 轮询 + 零 `--wait` 单次 guard」，避免 staged 文件风暴。
