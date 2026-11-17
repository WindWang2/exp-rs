# PATH_OWNERSHIP — R32-T05

## 本轨写入（基线 53412c86 上的增量）

- `src/agent/agent_copilot_dock_widget.{h,cpp}` — #2073/#2074/#2075
- `src/agent/view_control_service.h`、`src/agent/raster_display_service.h` — #2074 QPointer
- `src/agent/runtime/pi_process.{h,cpp}` — #2076/#2077/#2080
- `src/agent/runtime/agent_tool_host.{h,cpp}` — `hasProjectContext()`（#2073）
- `src/agent_ops/operations_coordinator.cpp` — #2078
- `src/agent_loop/decision_record.cpp` — #2079
- `pi/product-runtime/protocol.mjs` — #2080（导出 redactCheckpoint + 阈值）
- `pi/product-runtime/test/checkpoint-redaction.test.mjs` — 新增（无 SDK 依赖）
- `src/sdk/exprs/plugin_package.cpp`、`plugin_snapshot.cpp`、`plugin_discovery.cpp`、`plugin_registry.cpp` — #2111/#2112
- `src/plugins/host/CMakeLists.txt`、`scripts/stage_windows_application.ps1` — #2109
- `tests/test_bilingual_delivery_ui.cpp` — #2081 fixture 侧（整文件归本轨）
- `tests/test_pi_process.cpp` — #2076/#2077 新用例 + fixture 模式
- `tests/test_r23_e_python_worker_typed.py` — #2107
- `tests/CMakeLists.txt` — #2106（RUNTIME_OUTPUT_DIRECTORY/LABELS）+ 追加 R32-T05 测试块（含 PR #2131 亦追加块的潜在文本相邻，rebase 逐段）
- `tests/test_r32_t05_agent_dock.cpp`、`tests/test_r32_t05_ops_sdk.cpp` — 新增
- `.planning/r32-t05-agent-runtime/**`、docs（review 摘要）

## 明确不写

- `resources/translations/sicnu_zh_CN.ts`、lupdate 门禁（T06）
- `src/processing/framework/tool_call_dispatcher*`、TaskCenter/OutputCommitter（T04）
- `src/operators/framework/model_catalog.*`、`src/runtime/gpu/**`（T02）
- `workflow_schema.*`（T04）、CLI 主入口（T04）
- `resource_guard` 默认值、CI workflow 触发（.github/workflows/ci.yml 本次只读；#2106 的修复选 target 侧路径，不改 workflow 文件）

## Open PR 排他（重查于 2026-11-17）

- #2131（R31-T02）：`agent_copilot_dock_widget.cpp`（insecure_endpoint 状态处理 + finishRunUi 的 m_piSessionActive 条件）、`pi_conversation_runtime.cpp`（startSession 入口拒绝）、`pi_process.cpp`（start() 入口拒绝 + config 重置）、`llm_streaming_client.*`、`prepare_pi_runtime.py`、`tests/CMakeLists.txt`（末尾追加块）。全部为 AS-015 端点安全主题，与本轨 12 项修复无语义重叠；同文件不同函数，rebase 局部解决。
