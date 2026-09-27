# THREE_SURFACE_FACTS — MCP/GUI 侧口径取证（WP-D 原始材料）

来源：只读 Explore 子代理（2026-09-27，2.17M tokens，61 次工具调用）。全部事实带 文件:行号。CLI 侧事实见 BASELINE.md。

## A. MCP 分发与错误框架

- 分发链 `src/agent/mcp_server.cpp:634-913`：`scientific:agent_session`(666) → `isDataPlatformTool`(690) → meta 工具链(707-880) → SpatialToolRegistry(882-892) → 前缀路由(893-897) → 未知 → `-32602 "Unknown tool: <name>"`(911)。
- **未知实参不报错**：handler 只读已知 key（唯一例外：spatial 工具 `validateAgainstRequired` 2776-2782）。tools/call 不做 JSON-Schema 校验；声明的 required ≠ 运行时强制。
- `McpToolError`（mcp_server.cpp:81-96）：errorCode/errorCategory/rpcCode(-32000)/retryable → `sendToolErrorResult`(993-1019) 返回 result 对象 `{content:[{type:text,text:<redacted>}],isError:true,errorCode?,errorCategory?,retryable}`。
- data_platform_tools 的 `fail()` 抛 `std::runtime_error`（data_platform_tools.cpp:139）→ **isError:true 无结构化 code**（mcp_server.cpp:974-978）——与 CLI 侧结构化退出码的口径差（WP-D 记录，不越界修 MCP）。
- 分页默认 `mcpPageLimit`(237-242)：≤0→50，clamp 1..500。
- workspace 沙箱 `SICNU_MCP_WORKSPACE` + `validateWorkspacePaths`(1648-1662) → `PATH_OUTSIDE_WORKSPACE`（#1334 领地，只读）。

## B. 三面共用真源（对照表的"同源"锚）

1. **AtomicAlgorithmRegistry**（sicnu::processing）：CLI `run` 同步 adapter（cli_commands.cpp:428-443）；CLI `algorithms` 列表（cli_commands.cpp:334）；MCP list/get_algorithm_schema + execute_algorithm（mcp_server.cpp:1393,1740,1687-1730）；GUI 对话框/batch（raster_processing_dialog_base.cpp:441-451、batch_processing_dialog.cpp:223,623）。
2. **RSOperatorRegistry**（sicnu::operators）：CLI --list/--schema（main_cli.cpp:291-309）；MCP list/get_operator_schema/execute_operator（mcp_server.cpp:1553,1599,1733）；GUI 算子目录（rs_operator_catalog_panel.cpp:99-103）。
3. **DatasetStore/ExperimentStore**：各自实例化同一类——MCP 每调用新开（data_platform_tools.cpp:200-226）；CLI（cli_dataset_commands.cpp:174,235,503,618）；GUI（dataset_experiment_panel.cpp:186-238）。
4. **WorkflowRunCoordinator（单例）+ TaskCenter**：MCP run/resume/status（mcp_server.cpp:2570,2732,2644）；CLI workflow/pipeline run/resume → RsPipelineRunner（rs_pipeline_runner.cpp:550; cli_commands.cpp:523,593）；GUI WorkflowSessionController（workflow_session_controller.cpp:461,518,345）。
5. **执行路径差异（口径要点）**：CLI run=同步 adapter→退出码合同；MCP execute=dispatcher.submit 异步→execution_id；GUI=JobEngine。

## C. MCP 关键工具实参表（与 CLI 对齐用）

| 工具 | 实参 | CLI 对应 | 默认值 | 错误行为 |
|---|---|---|---|---|
| list_algorithms | limit,cursor | algorithms list（无分页！CLI 全量） | 50/0 clamp 1-500 | — |
| search_algorithms | query,group,tag,purpose,task,modality,input_type,output_type,large_raster_safe,limit,cursor | algorithms search（同名过滤器） | limit 50 | 非法过滤组合 `INVALID_QUERY`/validation(1500)；0 命中附 hints(1526) |
| get_algorithm_schema | algorithm_id | algorithms schema <id> | — | 缺/错 → runtime_error "Algorithm not found"(1738) **无 code**；CLI 同语义 → 退出码 5 |
| preflight_algorithm | algorithm_id,parameters{} | （CLI 无对应；经 batch/pipeline preflight） | — | PATH_OUTSIDE_WORKSPACE validation |
| execute_algorithm | algorithm_id,parameters{} | run <id> --param k=v | — | 未注册 → "Algorithm not found" 无 code；CLI → 5 |
| list_operators / get_operator_schema / execute_operator | operator_id… | （CLI 3.0 无 operator 直呼；legacy --list/--schema） | 50/0 | 同上形态 |
| run_workflow | pipeline(JSON str/obj), auto_load=false, experiment_*(experiment_db 给出时 experiment_id 必需), seed≥0 | workflow run <file> | auto_load false | 缺 pipeline → runtime_error(2468)；拒绝 → `INVALID_PIPELINE`(2577)；seed<0 → `INVALID_ARGUMENTS`(2537)；CLI schema 错 → 退出码 2 |
| get_workflow_status | pipeline_id(int) | （CLI 无 status 子命令；list-runs 近似） | — | 失败 → "Invalid or missing pipeline_id"(865) |
| resume_workflow | run_id ^[A-Za-z0-9_.-]+$ 禁 .. | workflow resume <id> / pipeline resume | — | 空/非法 → `INVALID_PARAMETER`(2724)；失败 → `RESUME_FAILED`/execution(2735)；CLI → 3（ExecutionFailure）|
| describe_dataset | layer_id | data inspect / dataset inspect | — | 读参 757-759 |
| get_lineage | asset_id | project lineage / data identity | — | 无 manager → "Data manager is not available"(2149)；不存在 → "Asset not found"(2159)；CLI lineage 文件缺失 → 6 |
| list_tools / search_tools / get_tool_schema / get_tool_help | compact 默认 true；query,group,tag… | tools list/search/schema | compact=true | 未知 → "Unknown tool"(2397)；CLI tools schema 未知 → 5 |
| scientific:agent_session | action 必需；goal/intent/mode/journal_directory/session_id/refs/domain(默认 research)/role/approve | session <action> | domain research | 缺 action → `INVALID_PARAMETER`(2701)；无 driver → `AGENT_OPS_UNAVAILABLE`/availability(2694)；CLI 同语义 → 7 |
| dataset:* 家族（list/inspect/version/diff/stats/validate/label_schema/split_inspect/leakage_audit/qa/sample_query/versions/splits） | dataset_db **必需**、limit 50、cursor 0 | dataset <sub> --dataset-db <path> | limit 50 | 缺 dataset_db → "dataset_db is required"(200-204) **无 code**；CLI 同语义现 → 1（改 6） |
| experiment:* / reproducibility:* | experiment_db 必需 | experiment / reproduce | — | 同上(214-218) |
| data:asset_passport | — | passport --path | — | — |
| preflight:check | — | （CLI 无独立口） | — | — |

## D. GUI 侧口径

- CommandDefinition（command_registry.h:31-54）**无参数/默认值字段**——命令层只承载能力；参数在各 dock/dialog 收集。GUI 命令 id 注册：main_window_workbench.cpp:177 + command_defs.cpp:48-812（全部内置）；插件命令 `plugin.<id>.<n>`（plugin_command_defs.cpp:28）。
- 语义映射：数据加载 layer.addRaster/addVector（command_defs.cpp:124-134）；工程 project.new/open/save（55-84）；算法执行 rs.bandMath/rs.spectralIndex/…（482-569，rasterTool 批量注册）；导出 cartography.export png/pdf/svg+sha256（383-395）；lab teaching.labCockpit.show（805-811）；experiment workbench.datasetExperiment Ctrl+Shift+E（294-302）；workflow workflow.new/…/run/stop（577-633）→ WorkflowSessionController。
- 参数默认值位置：对话框构造（例 contrast_stretch_dialog.cpp:99-110 clip/stddev=2.0）；JobRequest source="dialog"（raster_processing_dialog_base.cpp:447-455）。
- mission 命令与 agent mission:* 工具共用 applyMissionAction（command_defs.cpp:771-773 注释；mission_tools.cpp:578）——GUI/agent 同源范例。

## E. WP-D 对照操作池（≥15 行的候选，含三面入口锚）

1. algorithms list ↔ list_algorithms ↔（GUI 算子目录面板 rs_operator_catalog_panel）
2. algorithms search 过滤器语义/分页 ↔ search_algorithms ↔ 同上面板搜索
3. algorithms schema ↔ get_algorithm_schema ↔ 算子帮助（algorithm_help_catalog）
4. run <id> --param ↔ execute_algorithm ↔ rs.* 命令对话框（JobEngine）
5. workflow run ↔ run_workflow ↔ workflow.run
6. workflow resume ↔ resume_workflow ↔（WorkflowSessionController resume）
7. workflow list-runs ↔ get_workflow_status（近似）↔ workflow dock
8. dataset inspect ↔ dataset:inspect ↔ workbench.datasetExperiment
9. dataset create ↔ （MCP 无 create——差异行）↔ 同面板
10. dataset stats ↔ dataset:stats ↔ 同面板
11. dataset validate ↔ dataset:validate ↔ 同面板
12. dataset label-schema ↔ dataset:label_schema ↔ 同面板
13. dataset leakage ↔ dataset:leakage_audit ↔ 同面板
14. experiment list/compare ↔ experiment:list/compare ↔ experiment_studio
15. reproduce export/validate ↔ reproducibility:export/validate ↔ experiment_studio
16. passport --path ↔ data:asset_passport ↔（GUI passport 面板?）
17. session <actions> ↔ scientific:agent_session ↔（agent copilot）
18. project lineage ↔ get_lineage ↔（图层面板 lineage 视图）
19. env-doctor ↔ preflight:check（近似）↔ 启动自检
20. catalog export ↔ （MCP 无）↔（GUI 无）— N/A 行

差异维度：参数名（--dataset-db vs dataset_db）、默认值（limit 50 vs CLI 无分页）、错误口径（CLI 退出码合同 vs MCP isError 无 code）。
