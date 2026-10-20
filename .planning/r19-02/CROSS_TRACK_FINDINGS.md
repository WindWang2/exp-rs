# R19-02 CROSS_TRACK_FINDINGS

merge_loss_audit.py 全史审计（540 merges, 224 findings）中不属于本轨写区的线索。
检测命令：`python3 scripts/dev/merge_loss_audit.py --json`（详见 scripts/dev/README.md）。

## CT-1 [轨 03 / app+teaching] merge c8f5e0805422 — 14 路径丢失，13 个 today-still-base
- 涉及：`src/app/shell/{rs_job_panel,task_panel_host,workflow_session_controller}.cpp/.h`、`src/app/teaching_admin/teaching_admin_dock.cpp`、`src/app/widgets/rs_result_summary.cpp`、`src/help/operator_help_provider.cpp`、`src/teaching/course_home_view_model.cpp`、`src/teaching_admin/batch_assessment.h`、`data/help/operators/{data_io,optical}.json`、`tests/test_rs_result_summary.cpp`
- 这些文件当前 master 内容 == 该合并的 merge-base 版本 → 丢失的变更至今未归。
- 归属：`src/app/**`、`src/gui/**` 为轨 03 只读区；teaching 域在 #1664 (r18-e) 处理中。本轨不改，仅报告。

## CT-2 [r18-a / #1663 处理中] merges 4c906605169e 与 2a86f654c666 — 各 6 相同路径
- `src/analysis/segmentation/rs_multires_segmenter.cpp`、`src/operators/rs/rs_sam_classify_operator.cpp`、`src/processing/algorithms/image_fusion.cpp`、`tests/{test_image_fusion,test_obia_segmentation,test_spectral_classification}.cpp`
- 与 r18-a PR #1663 的"恢复 #1514 被静默回滚的修复"叙事一致（检测器独立交叉验证其必要性）；严格只读，勿重复搬运。

## CT-3 [数据/基准] merge 68ac5398bf3a — benchmarks/observatory 15 文件 + pi/knowledge 1 文档
- 观测基准 JSON（obs_dataset_*、obs_governance_*、obs_io_*、obs_taskcenter_*）在合并中丢失后未归。若这些基准仍被引用需复核；本轨不处理。

## CT-4 [文档] merge 899eb571883e — ADR 0146 (lab-sample-foundry-manifest-contract) 丢失
- docs/adr/0146 文件在合并后消失；如该 ADR 仍是有效决策需重录。本轨不处理。

## CT-5 [测试] merge 2a0147cdaa18 — tests/test_classification_e2e.cpp 修改丢失
- 单测试文件修改至今未归；分类域接近 r18-a 范围。本轨不处理。

## 本轨已处理
- merge 5e14bd1d7b（science_context 6 文件，#1518/#1505-P1 修复）→ 本轨 T0 恢复（a29f935e0）。
- merge 29596c913（30 路径）→ 已由 r18-c (#1660) 恢复，检测器用作校准基准。

## CT-6 [轨 03 / cartography 源码] surface 快照两腿契约在 5 个 cartography 工具上互斥
- r18-C (#1660) 的规范化投影让 tools/list/get_tool_schema/快照对 property-less schema 自动补 `"properties": {}`；而 test_surface_snapshot 的 registry 腿钉的是 **raw** SpatialToolRegistry schema。两腿只有 raw 本身规范化形态一致时才同时成立。
- `workspace:undo/redo`（本轨区）已在 3ff4e0ba4 修为 canonical 形态；**cartography:{catalog_index,chart_list,lint_catalog,list_rules,list_token_sets}** 的 raw schema（src/agent/cartography/**，轨 03 红区）仍缺 properties → registry 腿确定性失败（KNOWN_BASELINE_FAIL，失败信息含工具名与 regen 提示）。
- 建议归属：轨 03 / r18-d 后续 —— 5 个工具的 inputSchema() 补 `"properties": {}`（与 workspace 工具同型，一行式修复），无需动测试或快照。
