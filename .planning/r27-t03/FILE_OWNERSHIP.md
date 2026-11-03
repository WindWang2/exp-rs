# R27-T3 文件归属（写入面）

本轨写入（白名单内）：
- src/agent_ops/{repair_approval,session_surface,operations_coordinator,secret_redactor,ops_driver}.{h,cpp}
- src/app/agent_ops/agent_ops_control_center_dock.{h,cpp}（panel 若需）
- src/agent/{mcp_server.cpp,llm_streaming_client.cpp,agent_copilot_dock_widget.cpp,tool_catalog/workspace_containment.h,tool_catalog/meta_protocol_tools.cpp}
- src/sdk/exprs/external_process.cpp
- src/cli/{rs_pipeline_runner,main_cli}.cpp（+cli_commands.cpp SPP-12 若做）
- src/python/{scripts/worker_daemon.py, sdk/exprs/plugin.py, qgis_python.cpp(SPP-8/13), isolated/*(SPP-5/11 若做)}
- src/plugins/host/plugin_host_worker_main.cpp（SPP-5 若做）
- pi/mcp_bridge.ts（AS-011）
- tests/test_r27_t03_*.cpp（新增）+ tests/CMakeLists.txt（append-only 追加块 + 本次撕裂修复）
- 不写：src/processing/**（#1763 门建于 src/agent）、其他 src/app/**、docs/adr/**、根 CMakeLists
