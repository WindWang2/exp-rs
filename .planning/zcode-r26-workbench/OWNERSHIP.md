# OWNERSHIP — R26-02 workbench

## 本轨独占写区（无开放 PR 冲突，已核对 5 个 PR changed-files）
- src/app/classification/** （全部）
- src/app/widgets/** （spectral_profile_widget, spectral_workbench_panel, roi_statistics_widget, histogram_widget, histogram_stretch_widget, band_role_combo, guided_workflow_widget 等）
- src/app/dialogs/** （change_detection_dialog, fusion_dialog, radiometric_calibration_dialog, qa_mask_dialog, orthorectification_dialog, atmospheric_dialog 等；无 PR 占用）
- src/app/teaching/** （guided_lab_workspace.cpp, lab_cockpit_dock.cpp — 注意：src/app/teaching_admin/ 被 #1725 占用，不碰）
- src/help/**（测试增强）
- tests/test_r26_workbench_*.cpp（新测试）+ tests/CMakeLists.txt 尾部追加块

## 只读区（等 #1724/#1725 合并）
- src/app/main_window*（含 main_window_processing.cpp）、src/app/cartography/、src/app/shell/、src/agent/mapspec/
- src/app/workbench/command_defs.cpp、selection_context.*（#1724）
- src/lab/、src/teaching_admin/、src/app/teaching_admin/（#1725）
- src/data/import_preflight.*、src/geospatial/products/、src/operators/、src/processing/（#1726/#1728）
- pi/、src/agent/mcp_server.*、src/workflow/（#1727）

## 共享注册文件
- tests/CMakeLists.txt：仅尾部 `# BEGIN R26-workbench` ... `# END R26-workbench` 追加块
- 不改 CMakeLists.txt / src/app/CMakeLists.txt（无新目录/目标需求时）
