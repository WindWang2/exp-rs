# R16-A Baseline — 证据核验与可复现评价闭环

采集时间:2026-10-17(UTC)· 采集人:R16-A 集成 Agent(宿主:ZCode,非 Google Antigravity)

## 执行环境如实记录(宿主适配声明)

- 本任务 Prompt 面向 Google Antigravity 的 `/teamwork-preview` 与 `/goal-loop`。当前宿主为
  ZCode,不存在这两个命令。按 Prompt 最高优先级约束第 1 条,已改用语义等价的原生机制:
  - `/teamwork-preview`(多子代理并行协作)→ ZCode Agent 工具后台并行子代理 + 三个相互隔离的
    git worktree(t01/t02/integration)。
  - `/goal-loop`(Oracle 驱动循环)→ 按 Prompt 内嵌的"五阶段 Oracle-Driven Goal Loop"协议
    执行,Oracle 判定写入 `.planning/r16-a-t01/GOAL_LOOP_LEDGER.md` 与
    `.planning/r16-a-t02/GOAL_LOOP_LEDGER.md`,不更新全局 ledger。
  - 未读取也未执行 https://github.com/bravesd/goal-loop 的任何脚本;协议以 Prompt 内嵌文本为准。
- 所有命令结果均为真实执行输出,无伪造。

## Git 基线

- `git fetch origin --prune` 后 `origin/master = 46791bc65c6f552ebbcf723bb91892736d56f319`
  (= 2026-10-16 参考 SHA,无更新提交;该 SHA 是执行时快照,非永久基线)。
- 主仓工作树 clean(`git status --porcelain=v1` 为空),未覆盖任何未提交修改。
- `gh auth status`:已登录 WindWang2(keyring),token scopes 含 repo/PR 写权限。

## Worktree 布局(全部新建,无同名冲突)

| Worktree | 分支 | 用途 |
|---|---|---|
| /home/kevin/project/exp-rs-r16-a-t01 | hardening/r16-a-t01-verification | 轨道 1:src/verification/** |
| /home/kevin/project/exp-rs-r16-a-t02 | hardening/r16-a-t02-grader-faultlab | 轨道 2:src/grader/** + src/faultlab/** |
| /home/kevin/project/exp-rs-r16-a-integration | hardening/r16-a-evidence-verification | 集成 + 独立评审 + PR |

三个分支均自 origin/master (46791bc65) 新建;`git show-ref | grep -c r16` = 0,无既有同名分支。

## 构建纪律

- 仓库规约(CLAUDE.md):`cmake --preset dev-default`,build 目录 `<worktree>/build-dev`,
  并行上限 **-j2**(CLAUDE.md 明确禁止 -j$(nproc);R16 任务书上限 -j4,取两者更严的 -j2)。
- 共享构建租约:`flock /home/kevin/project/exp-rs-build.lock`(供五个并行 Prompt 共用)。
- CTest `-j1`,`QT_QPA_PLATFORM=offscreen`。
- 目标模块均为叶子库(sicnu_verification / sicnu_grader / sicnu_faultlab 仅链 jsoncpp,
  零 Qt/QGIS),对应测试目标可独立快速构建,不需要全量编译主程序。

## 阶段 0 结论(先读后改)

- R12-R15 已多轮合并;R14-E(教学/评测/证据链)已交付 9 项修复并验证 #1464/#1488 主干侧
  已修复(#1488 的异常屏障部分当时属开放 PR #1509,现已合并入 master)。
- 因此本任务的正确姿态是:**重新核验**当前 master 上这些修复的真实状态,寻找仍然存在、
  可复现、有真实回归价值的缺口;严禁按旧 Issue 文本盲修、严禁制造重复代码。
