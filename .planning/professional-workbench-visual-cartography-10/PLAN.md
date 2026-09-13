# PLAN — 执行顺序与里程碑

## M0 Phase 0（本文件所在阶段）
1. worktree/baseline ✓；`.gitignore` 白名单 ✓；规划文件 ✓；configure ✓（见 EVIDENCE）。
2. 首次 commit：`.gitignore` + `.planning/`。

## M1 WP-A 统一对象身份（Phase 1）
- `object_identity.h`（ObjectRef/ObjectKind + 规则）。
- SelectionContext: notify* API + snapshot 字段 + ContextFacts/ContextRules 扩展。
- object_links 解析 helper（从 ProvenanceSection 收敛，ProvenanceSection 改调）。
- mcp context-provider seam + `workbench:context` 工具 + 装配。
- 测试：`test_object_identity`、`test_selection_context`（扩展）、`test_agent_workbench_context`。

## M2 WP-B Cartography bridge（Phase 2，最大）
- `src/operators/cartography/cartography_operators.*` 五算子 + 注册 integration commit。
- `src/app/cartography/cartography_dock.*` + 命令 + 预览 + 报告面板 + 装配。
- workflow preset（compose→preflight→export）。
- 测试：`test_cartography_operators`、`test_cartography_dock`、`test_cartography_workflow_bridge`。

## M3 WP-C Visual Analytics（Phase 3）
- va_data/va_source/va_chart_* + va_selection_hub。
- 消费者：结果可视化动作（混淆/转移/类面积）、zonal 图、光谱收敛。
- 测试：`test_visual_analytics`（数据正确性/有界/取消/状态机）、`test_va_brushing`。

## M4 WP-D + WP-E（Phase 4）
- 节点 IO 徽标 + 类型兼容 + 参数 inspector + preflight 投影；`test_pipeline_editor_v10`。
- view_link_controller + 命令 + `test_view_link`。

## M5 WP-F + 收尾（Phase 5）
- TaskPanelHost catalog UX（搜索/最近/收藏/过滤）+ `test_processing_catalog_ux`。
- a11y/theme parity 扫描（硬编码 QColor/accessibleName）、help catalog、100k 行分页复核。
- Lab 投影核查（guided workflow role/mode）、K 结构断言（offscreen smoke）。

## M6 对抗 review（Phase 6-7）：2 只读 subagents → P0/P1 清零。

## M7 最终验证 + PR（Phase 8）：TEST_MATRIX 全绿于最终 HEAD → rebase → push → PR。
