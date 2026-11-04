# ISSUE TRIAGE — R26-02 workbench（执行时复核）

## 指定核查 issue
- #1615 helpIdForCode/helpIdFromMessage 全量重建 → **FIXED_IN_MASTER**（修复于 60aaf2dac2，现位于 src/help/error_diagnostics_bridge.cpp:44-72 CuratedCodeTable static thread_local 备忘录；测试 test_r16_E_help_parsing_robustness.cpp:268,278）
- #1614 HelpCompact::toJsonText 预算不足切片 → **FIXED_IN_MASTER**（src/help/help_presenter.cpp:158-178 阶梯降级；测试 test_help_core.cpp:409-427）
- #1615/#1614 残留：极端预算（n<9）与备忘录增长回归无直测 → 本轨 WP10 补测试
- #1639 src/help 技术债（2 项）→ 需复核具体条目（与 #1614/#1615 同 commit 修复 2 项？标记 COVERED 待核）
- #1632 src/app 技术债（6 项）→ 需逐项核对，与本轨修复重叠部分标 COVERED_BY_R26_WORKBENCH
- #1695 ui lane 清理清单 → 部分条目可能与本轨重叠；逐项判定
- #1694 评分 checkpoint 摘要 → COVERED_BY_OPEN_PR #1725（batch_assessment.cpp 在其 changed-files）
- #1689 反馈包跨学生泄露 → COVERED_BY_OPEN_PR #1725（feedback_pack.cpp）
- #1535-#1537、#1559-#1565：R25-D/E 报告 master 已修/分担 → 逐条 gh issue view 复核后归档判定
- #1557 mac native → CROSS_TRACK 平台跨轨，OUT_OF_SCOPE
