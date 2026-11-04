# R26-02 workbench — GOAL

把 Qt/QGIS 工作台从"功能多但操作割裂"打磨为可直接用于本科遥感实验的高可用产品。
独占写区：src/app/classification/、src/app/widgets/、src/app/dialogs/（避开开放 PR changed-files）、src/help/、QSS/资源、GUI offscreen 测试。
受限/只读：src/lab/、src/teaching_admin/（等 #1725）、src/app/main_window*、src/app/cartography/、src/app/shell/、src/agent/mapspec/（等 #1724）、src/app/workbench/command_defs.cpp + selection_context.*（#1724 占用）、src/app/teaching_admin/teaching_admin_dock.cpp（#1725 占用）。

完成 Oracle：
1. 所有执行包 DONE_TESTED 或诚实标记 BLOCKED_NOT_DONE/SUPERSEDED
2. 每个缺陷修复：RED（修复前生产实现失败）→ GREEN×2（HEAD 连续两轮）
3. R1/R2 独立评审（未参与实现的上下文）P0/P1=0
4. 本地构建 + 定向测试两轮绿；git diff --check 干净；注册完整性脚本通过
5. gh pr create 成功返回 PR URL
