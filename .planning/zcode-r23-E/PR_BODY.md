# r23-E: plugin isolation, worker compat and delivery regression guards — #1662 sweep closure + #1715-class pre-push defense

**Base**: `master` @ `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（push 前经 fetch 复核）
**Head**: `agent/r23-e-platform-plugin-release-20261026T170017Z`（独立 worktree，6 commits）
**性质**：测试 + 工程门禁交付。**生产 src/** 零修改**（两轮独立 reviewer 各自核实 whitelist-clean；tests/CMakeLists.txt 59/0 纯尾部 append）。

## Why

PR #1662（全仓修复清扫）的 E 轨域修复——plugin host 类型门（#1551/#1592）、Python worker 兼容层（#1555/#1556/#1608/#1609）、native 生命期（#1557/#1617/#1618）——**全部没有回归测试**（4 个修复 commit 的 stat 零 tests/ 文件）。#1714→#1715 的 tests/CMakeLists.txt 合并事故证明五轨共享注册面需要机械 pre-push 防线。#1708 的 Windows `.cmd` 守卫腿从未在真实 cmd.exe 执行过。本轨补齐这三类缺口，并为 #1698/#1623 在当前 master 上重做实机安装证明。

## What（4 个测试/工具 + 注册块）

| 交付 | 守卫 | 判别力证据（突变 RED） |
|---|---|---|
| `tests/test_r23_e_plugin_typed_gates.cpp` | 恶意类型 plugin.load/execution 帧必须 typed E1002/E6002 拒绝、REAL worker 存活、后续合法流量可用 | 移除 services 门 → 1 败；移除 peerString 门 → 1 败；还原绿 |
| `tests/test_r23_e_python_worker_typed.py` | np 标量/ndarray typed 序列化、不可序列化 → -32000、unload 后菜单回调永不触发（marker 判别器）、陈旧 py: → -32602、伪 shm key → -32603 | #1609 回退 → 1 败；#1608 回退 → 2 败；#755 回退 → 1 败（R1/R2 独立复现） |
| `tests/test_r23_e_native_terminal.cpp` | openTerminalAtPath：终端选择 × cwd=目标目录 × 零 --working-directory 参数 | 探针三行（$0/$@/pwd）直接断言生产契约 |
| `scripts/check_merge_registration_integrity.py` | 注册并集（target+add_test+引号名）vs origin/master × 冲突标记（.md/.MD 排除）× 分支感知重复 × --require-refs fail-closed | 10+ 模拟合并场景：union 过、丢 target/add_test/引号名各自检出、标记检出、粘贴两次检出、--allow-removed 白名单过 |

## 实机交付证据（本轨新跑）

- **Linux（WSL GCC 16.2.1, Release, flock 串行, -j2/-j4 纪律留证）**：完整 `cmake --install` 树（GUI+cli+`sicnu_worker`+`sicnu_zh_CN.qm` —— **#1698/#1623 回归成立**）；净环境 `env -i` CLI `--help/--version` RC=0；**relocate**（整树搬移后复跑）；RPATH `[$ORIGIN/../lib:$ORIGIN]`×3（readelf）；`ldd` 零 not-found；`tests/test_r22_infra_bundle_guard.sh` 复跑 exit 0。冒烟 **12/12 PASS**（GUI 应用以"offscreen 事件循环启动成功"断言——窗口应用无 --help 契约，help/version 归 CLI，如实标注）。
- **Windows 真机 cmd.exe**（#1708 遗留 NOT_EXECUTED 补证）：5 恶意 `--version`（穿越/`..`/分号/&/空格）全部 exit 2 + refusing 文案 + canary 存活；合法版本正确过门。
- CI YAML 双文件离线解析通过；**不等待线上 CI**。

## 双轮独立评审（均为 fresh subagent，scratch 克隆做破坏实验）

- **R1**：APPROVE-WITH-FIXES → P1×1（.md 排除 pathspec 无效）+ P2×3 + P3×4 → commit `b22f7ddd` 全闭环，R1 自己的验收反例全部翻转通过。
- **R2**：APPROVE-WITH-FIXES → 独立复现 R1 全部突变主张与修复闭合，新发现 P2×1（引号 add_test 双盲，master 现存 4 处）+ P3×4 → commit `cf3bfd04` 全闭环。
- 证据：`.planning/zcode-r23-E/REVIEW_R1.md` / `REVIEW_R2.md`（含各自验证命令与反例记录）。

## 跨轨反馈（CROSS_TRACK_FINDINGS.md，未改他轨文件）

#1699 残余 9 处墙钟断言逐条 file:line + 三分法转换方案（C/D 轨测试文件）；根目录 14 个遗留 .cmd 盘点；WSL 宿主 numpy 环境观察；master 既有 if/else 双 add_test 变体说明。

## Issues

不 closes（修复已在 master，GitHub 关闭权归维护者）。Progresses #1699（方案）、#1683（盘点）、#1682（Windows 实机脚本腿）。

## 测试/限制（诚实声明）

- Linux 全部实跑（ctest 7/7 + 冒烟 12/12）；Windows 脚本级实跑；**macOS NOT_EXECUTED**；**MSVC 编译 NOT_EXECUTED**（零生产改动；新测试只用跨平台 API、与 sibling 同构门控：`if(UNIX AND NOT APPLE)` / `if(Python_EXECUTABLE)`）。
- `r23_e_python_worker_typed` 在无 numpy/AF_UNIX 宿主：直接运行打印 skip 原因；ctest 下显示为 Passed（POSIX-only daemon 设计，已文档化）。
- 9525 边 GUI 全量构建未整体跑（定向 1234+1886+install 迭代闭环，主程序最终构建并安装）。

## Merge safety

push 前已跑：`git fetch origin --prune`（master 未前进）+ `check_merge_registration_integrity.py --require-refs`（HEAD⊇origin/master：1039 targets + 49 add_test，lost 0，markers 0，duplicates 0）+ `git diff --check` + `ctest -N` 目标在册。不自动合并；不等待 CI。
