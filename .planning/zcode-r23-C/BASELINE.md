# BASELINE — r23-C（Pi/MCP Harness、Qt/QGIS 工作台与制图交互闭环）

- 仓库：https://github.com/WindWang2/exp-rs（origin, https）
- 任务起草快照：master@ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea（2026-10-23 起草）
- **执行基线（fetch 后实测）**：origin/master = `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（与起草快照一致，master 未前移）
  - fetch 时间：2026-10-26T16:41Z（UTC）；`git fetch origin --prune` 干净返回
  - default branch：`master`（`gh repo view --json defaultBranchRef`）
- 分支/工作树：
  - 分支：`agent/r23-c-agent-workbench-20261026T164126Z`（自 origin/master 新建，无本地偏移）
  - worktree：`/home/kevin/projects/rs-studio/exp-rs-r23-C-20261026T164126Z`
  - 主 checkout（/home/kevin/projects/rs-studio/main）本地 master 落后于 origin/master（911e0b17a3 → ad98a7a704），只读使用，绝不直接改
  - 兄弟 worktree（r20/r22/hardening/flash/rs14 各轨）一律只读；本轨不共享任何 build 目录
- GitHub 状态（2026-10-26T16:4xZ 实测）：
  - Open PR：**0**（与起草时一致）
  - Open Issues：**144**
  - 最近 merged：#1715(2026-10-26), #1714, #1713, #1712, #1711, #1710, #1709, #1708（R22 六轨+修复）, #1673–#1677（R21 五轨）
- 工具版本（本机实测）：
  - cmake 4.4.4 / g++ (GCC) 16.2.1 / Ninja 1.13.2 / Python 3.14.8 / gh（已登录 WindWang2）
  - 内核 linux 6.18.55-2-cachyos-lts x86_64；RAM 62G（可用 ~45G）；磁盘 /home 755G 空闲
- 构建/测试约束：`CMAKE_BUILD_PARALLEL_LEVEL=2`、全局 `flock $HOME/.cache/exp-rs-build.lock` 串行大构建、`QT_QPA_PLATFORM=offscreen`、`ctest --parallel 1`；绝不用 -j$(nproc)
- 配置：`cmake -S . -B build-r23-C -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON` → exit 0（Generating done 29.2s）
- 主干健康度核对：
  - tests/CMakeLists.txt 无 `<<<<<<<`/`=======`/`>>>>>>>` 冲突标记残留（#1715 修复后），configure 成功即证明
  - `.planning/r22-*` 各轨 FINAL_REPORT/REVIEW_R1/R2 存在（review 证据在仓库内，GitHub 平台无正式 Review 对象，不得称平台 APPROVED）

## 本轨权属（详见 OWNERSHIP.md）
白名单内生产可写：`src/agent/**`, `src/agent_loop/**`, `src/agent_ops/**`, `src/sdk/exprs/**`, `src/workflow/**`, `src/processing/framework/**`, `src/jobs/**`, `pi/**`, `src/app/**`, `src/ui/**`, `src/help/**`, `src/teaching/**`, `src/teaching_admin/**`, `src/verify/**` + 专属测试；`tests/CMakeLists.txt` 仅 append-only。

## 并行轨（执行中发现，2026-10-27 本地时间）
- 同一时刻存在 r23-A（exp-rs-r23-A-20261026T164112Z）与 r23-B（exp-rs-r23-B-20261026T164130Z）兄弟 worktree：任务契约预期的三轨并行。
- 实测：r23-A 持有 `$HOME/.cache/exp-rs-build.lock` 在构建（make，1621+ objects）；本轨 build-r23-C 的 flock 排队等待——绝不绕过全局锁。
- 后果：本轨大编译串行等待；push 前 tests/CMakeLists.txt 极可能与兄弟轨 append 交叉，必须重新 fetch + 逐字节对照合并（见 PR_COLLISION_MATRIX）。

## R22 修复现状关键核对（与 C0x 工作包对照）
- #1704/#1690/#1691/#1692/#1705（r22-agent, PR #1714）：代码已修（workspace_containment.h 的 R2 批注、appendLog 有界化、delta 通知、TerminalOutcome 收敛 #1640 等实测在树）。
- **R22-agent REVIEW_R2 遗留 R2-F3/R2-F6（TaskCenter cumulative/delta 索引空间混用）：未收口** —— `task_center.cpp` 累积分支（~:2039-2044）仍以 retained 窗口 size 对比绝对索引 forwarded，eviction 后守卫失真（丢行/重流行），为 C05 明确要求复查项。→ 本轨 WP-1。
- #1694/#1689/#1678/#1688（r22-teaching, PR #1711）：代码已修（feedback_pack owner-token、Unicode 边界、rubric 委托引擎等实测在树）。
- #1713（r22-workbench）9 项与 #1677（r21-D）编辑 Lease/MapSpec 项：已在树（本轨不重复修复）。
