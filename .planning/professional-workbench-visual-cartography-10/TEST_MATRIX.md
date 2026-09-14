# TEST_MATRIX

（执行记录追加于此；格式：套件 | 命令 | 结果 | HEAD。）

## 执行记录（第一轮矩阵）

| 套件 | 命令 | 结果 | HEAD |
| --- | --- | --- | --- |
| test_object_identity | ctest -R "objectKindToken\|primaryObject\|selectedLayerIds\|resolveSelectionAssetTargets\|workbenchContextToJson" -j1 | 9/9 Passed | 本轮 |
| test_agent_workbench_context | 同上（workbench:context 用例） | 4/4 Passed | 本轮 |
| test_selection_context / ContextFacts | ctest -R "SelectionContext\|ContextFacts" -j1 | 8/8 Passed | 本轮 |
| test_provenance_section | ./test_provenance_section | 6/6 cases, 61 断言全绿（修复 i18n 期望漂移后） | 本轮 |
| test_command_registry | ctest -R "CommandRegistry\|command registry" | 9/9 Passed | 本轮 |
| test_shortcut_conflicts | ctest -R "Shortcut\|shortcut" | 7/7 Passed（修复 GCC16 编译后） | 本轮 |
| test_mapspec（+ operators10） | ./test_mapspec | 226 cases, 601/602 断言（渲染确定性 1 项=本机环境，master 同） | 本轮 |
| test_visual_analytics | ctest -R "VA payloads\|VaDataSource\|VaChartWidget" | 5/5 Passed | 本轮 |
| test_view_link | ctest -R "view link" | 3/3 Passed | 本轮 |
| test_processing_catalog_ux | ./test_processing_catalog_ux | 4/4 Passed | 本轮 |

（sicnu_geo_rs / sicnu_geo_rs_cli / 第二轮回归矩阵在 app 构建完成后补记。）

## 执行记录（第二轮：app/CLI 构建 + 扩展回归矩阵）

| 套件 | 结果 |
| --- | --- |
| sicnu_geo_rs（GUI app, 279MB）/ sicnu_geo_rs_cli | Built，零 error |
| test_i18n | 34 断言全绿 |
| test_theme_selector_parity | 85 断言全绿 |
| test_ui_task_center_contract | 174 断言全绿（thin-client 法扫描含新面板文件无违规） |
| test_agent_canvas_sync | 5 断言全绿 |
| test_processing_history_model | 42 断言全绿（修复 i18n 期望漂移：4/8 红→0） |
| test_catalog_pagination | 163 断言全绿 |
| test_workbench_host | 50 断言全绿 |
| test_workbench_state_model | 29 断言全绿 |
| test_workbench_shutdown_policy | 54 断言全绿 |
| test_command_palette | 23 断言全绿 |
| test_inspector_host | 27 断言全绿 |
| test_qgis_display_manager | 343 断言 21/21 全绿 |
| test_selection_context | 60 断言 13/13 全绿（修复 i18n 期望漂移） |
| test_agent_tools_3 | 126 断言全绿（MCP 前缀路由回归） |
| test_classify_workflow_controller | 30 断言全绿 |
| test_agent_golden_workflow | 26 断言全绿 |
| test_agent_workflow_executor | 18 断言全绿 |

i18n 期望漂移修复清单（PR #953 机械重写源文未同步测试；master 上皆红）：
test_provenance_section、test_processing_history_model、test_selection_context。

## 最终矩阵（Phase 8，最终 HEAD = rebase 后 cae17355ee 系列 + review 修复 commit）

- 状态：rebase origin/master → up to date（无新提交）；`git diff --check` 干净；
  冲突标记扫描为空；秘钥扫描为空；存在性断言两条全过；措辞检测 0 命中。
- **26/26 套件全绿**（列表见上两轮矩阵）+ test_mapspec 601/602 断言
  （唯一失败 = test_cartography_visual.cpp:230 PNG 渲染确定性，本机字体环境，
  master 上同样失败 —— 见 OUT_OF_SCOPE）。
- sicnu_geo_rs + sicnu_geo_rs_cli 于最终 HEAD 构建零 error。
