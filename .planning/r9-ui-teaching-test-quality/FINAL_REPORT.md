# R9 Wave 1 FINAL REPORT — 方向 C（UI/教学/测试质量收敛）

日期: 2026-10-12 ｜ 分支: hardening/r9-ui-teaching-test-quality ｜ BASE_SHA: 8a08642760e2e8ffd2c4b965fc4683c01d3e4758 ｜ worktree: exp-rs-r9-ui-teaching

## 已关闭 Work Packages
| WP | 内容 | Commit | 验证 |
|----|------|--------|------|
| C1-1 | faultlab fail-before-mutate | 7430953dc | RED(8.0==4.0×2)→GREEN 2/2 ×2 |
| C1-2 | gww Next 按钮任务态门控 | c8bd0a52a | RED(!true)→GREEN 11/11 ×2 |
| C1-3 | rs_scan_pool 销毁 owner tombstone | 64b7901c9 | RED(恰好2)→GREEN 10/10 ×2 + 下游 27 用例 ×2 |
| C1-4 | interpreter 启动探测 | b4bf909d9+911a967a6 | RED(3)→GREEN 7/7 ×2 (Win 实机) |
| C1-5 | 截断标志 fail-closed 传播 | b4bf909d9+911a967a6 | 同上 |
| C2 | master 去重审计 | (subagent 报告入矩阵) | 18 子项判定 |

## 编译/CTest 成绩（Windows/MSVC，-j2/-j1，QT_QPA_PLATFORM=offscreen）
- 定向目标全编译成功: test_faultlab_params_r8, test_scan_pool, test_teaching_admin_script_adapters_r8, test_guided_workflow_widget(1659 步), 4 个下游目标(490 步)
- 双轮全绿: faultlab 2/2, scan_pool 10/10, adapters 7/7, gww 11/11, histogram+roi 6/6, state_mirror 18/18, async AS1-3 3/3
- pre-existing (基线复现取证): async AS-4/AS-5 writeValidProject fixture 失败

## Review
- R1 reviewer-②: APPROVE_WITH_CHANGES → P2/Nit 修复于 911a967a6
- R2 reviewer-③ (delta): APPROVE, 无 P0/P1

## Issue 处置
- #1474 五条 unresolved review threads 全部闭合（不重做 master 已修部分）
- ALREADY_FIXED_IN_MASTER: #1453 全部、#1464 全部、#1465/#1447 全部已修子项、#1466 部分、#1468 全部
- 后续 wave 候选: #1466 batch discovery 静默丢弃; #1465 refreshRecipes 链路类型门; dock 同步执行后台化 (WP4)

## 未验证条件
- Linux/macOS lane (NOT_VERIFIED, 代码平台中立)
- 线上 CI (按任务书不创建/不等待)

## 遗留
- 见 RISK_REGISTER.md 与 PR 风险节
