# R9 方向 C：UI/教学/测试质量收敛 — 目标

仓库: WindWang2/exp-rs ｜ 分支: hardening/r9-ui-teaching-test-quality ｜ BASE_SHA: 8a08642760e2e8ffd2c4b965fc4683c01d3e4758

## 使命
修复 Qt/QGIS 桌面 UI、教学闭环、帮助与跨平台测试质量问题；只完善当前模块，不拓展产品方向。

## 本波（Wave 1）范围 — #1474 五条未 resolved review threads
| # | 文件 | 问题 | 状态 |
|---|------|------|------|
| C1-1 | src/faultlab/fault_transforms.cpp | cell_size 在 duplicate/relocate 目标验证前发布；拒绝的 transform 仍变异输入 | TODO |
| C1-2 | src/app/widgets/guided_workflow_widget.cpp | 异步 operator step 提交后 Next 仍可点（onNextStep 静默忽略） | TODO |
| C1-3 | src/app/widgets/rs_scan_pool.cpp | owner 销毁后 forgetOwner 抹掉保护记录；1024 次 cancel 触发 pruning 后旧 worker 误判有效 | TODO |
| C1-4 | src/teaching_admin/script_adapters.cpp | Windows findExecutable("python") 可返回 WindowsApps Store alias stub，未探测即采用 | TODO |
| C1-5 | src/teaching_admin/script_adapters.cpp | verifyOfflineBundle/checkLabPackDrift 丢弃 stdout/stderr truncated 标志 | TODO |

## Oracle
每个修复: 失败复现(red) → 最小修复 → 定向 CTest 两次通过(green) → 证据入 LEDGER。
