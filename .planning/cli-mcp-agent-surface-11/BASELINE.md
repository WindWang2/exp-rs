# BASELINE — cli-mcp-agent-surface-11

Phase 0 只读审计原始记录。审计时间：2026-09-16；主仓库 `/home/kevin/projects/rs-studio/main`。

## GitHub / origin 事实（启动时刷新）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （`fix: fail-closed fixes for review issues #994–#999 (#1000)`）。
- Prompt 快照中的 #991/#992 **均已合并**（在 master log 可见：`c5d4aafe8e` = #991 D18 workbench，
  `1cea98921b` = #992 D19 foundry/benchmark）。按规则以新事实为准，不保留旧假设。
- 启动时 open PR（共 2 个）：
  - **#1008** `zcode/radiometric-spectral-workbench` — D13 radiometric/6S/spectral workbench，
    mergeStateStatus=DIRTY。Changed files：`src/analysis/atmospheric|hyperspectral`、
    `src/core/radiometric_state|spectral_library`、`src/app/widgets/spectral_*|band_composite_*`、
    `src/processing/algorithms/radiometric_calibration|spectral_*`、
    `src/agent/spatial_tools/spectral_spatial_tools.*`、`src/agent/spatial_tools/spatial_tool.cpp`、
    `src/agent/CMakeLists.txt`、`src/*/CMakeLists.txt`、`tests/test_*spectral*|test_*radiometric*|test_continuum*|test_fast_6s*`、
    `tests/CMakeLists.txt`、`.gitignore`、`docs/adr/0158`、`.planning/radiometric-spectral-workbench/**`。
  - **#1009** `zcode/execution-runtime-convergence-11` — 执行运行时收敛（chunk contract/resume/governor/lease），
    mergeStateStatus=UNSTABLE。Changed files：`src/runtime/**`、`src/operators/framework/**`、
    `src/processing/framework/{fused_chain,local_worker_pool}.*`、`src/workflow/pipeline_run_coordinator.cpp`、
    `src/agent/data_platform_tools.cpp`（+4 行 include 修复）、`tests/test_chunk_*|test_execution_*|test_worker_lease_*`、
    `tests/CMakeLists.txt`、`CHANGELOG.md`、`data/help/diagnostics.json`、`docs/execution/**`、`.gitignore`。
- Open issues #1001–#1007（全部为 io/workflow/dataset/agent/georef 域 R2 残留 bug）：
  - #1001 io:clip srcCrsOverride 误作 targetCrs（io 域）
  - #1002 makeRegistryNodeExecutor fail-open（workflow 域）
  - #1003 joinFeaturesBySampleId JSON-null 列（dataset 域）
  - #1004 dataset:qa scan_capped 时 identity 仍 Pass（agent 数据工具域）
  - #1005 mapPickToLayerCrs 异常时返回未变换点（georef 域）
  - #1006 PipelineRunCoordinator soft-default syntheticExecute（workflow 域）
  - #1007 dataset:qa 不审计 CRS（dataset 域）
  逐条与本 track（CLI/MCP/Pi surface）**零交集**（它们是数据/域语义缺陷，不是 surface 协议缺陷），
  全部登记 EVIDENCE `OUT_OF_SCOPE`，不实施。
- `ISSUES.md`：确认为 D3 教学 lab 内容 backlog（T-1..C-2 算子缺口），非实时 backlog，不实施。
- 其他远端分支（`git branch -r --sort=-committerdate` 前 20）：均为已合并 track 的残影 +
  上述两个 open PR 分支。无第三个并发 surface track。

## 本 track 文件级交集判定

| Open PR | 与本 track primary scope 交集 | 策略 |
|---|---|---|
| #1008 | `src/agent/CMakeLists.txt`（append spectral sources）；`src/agent/spatial_tools/spatial_tool.cpp`（注册 spectral 工具）——非我 scope 但同文件风险 | 我的 src/agent 修改只落在 `src/agent/mcp*` 与 `src/agent/tool_catalog/**`（#1008 均未触碰）；对 `src/agent/CMakeLists.txt` 若需接线保持 append-only 最小 diff；不触碰 `src/agent/spatial_tools/**` |
| #1009 | `src/agent/data_platform_tools.cpp`（+4 行）；`tests/CMakeLists.txt`；`CHANGELOG.md`；`.gitignore` | 不修改 `data_platform_tools.*`（parity gate 只读消费）；`tests/CMakeLists.txt` append-only；CHANGELOG/.gitignore 最后 integration commit append-only |

## Surface 代码事实（Explore 子代理 #1 只读审计，2.4M tokens，75 tool calls）

### 五个并行 schema 源（无统一 parity gate）
1. `AgentToolCatalog`（src/agent/tool_catalog/agent_tool_catalog.h:53 单例）— 聚合
   algorithm/interaction/data/spatial 四类 provider；`AgentTool`（agent_tool.h:32–62）含
   jsoncpp `inputSchema/outputSchema`；导出 `toOpenAiToolDefinition/toMcpToolDefinition/toJson`。
2. `kMetaTools[]`（mcp_server.cpp:317–444）— ~22 个协议级 meta 工具手写 C-string 表 +
   `metaToolInputSchema()`（:448）。
3. `dataPlatformToolDefs()`（data_platform_tools.h:30–56）— dataset:/experiment:/reproducibility:/benchmark:
   工具手写 struct 表，与 catalog 完全不相交。
4. `InteractionToolRegistry`（interaction_tool_registry.h:38–45）— 自有
   `InteractionToolDefinition`；`revision()`（:97）仅供 catalog 缓存失效。
5. `AlgorithmDescriptor::toInputSchema`（processing/framework/algorithm_descriptor.h:63–131 的
   AgentMetadata 为真实能力 schema）。

辅助分离表示：`agent_tool_call_exporter.h`（OpenAI 导出，绕过 catalog）、`harness/tool_manifest.h`
（riskClass/taxonomy，“derived, never hand-maintained”）、`data/processing/algorithm_meta/*.json`
（38 个 sidecar，#707 descriptor 为唯一真值，mcp_server.cpp:536–556 drift log）、
`contracts/spatial_contracts.h`（`paginate`:135、`kMaxToolOutputBytes`=512KiB:139）。

### MCP server（src/agent/mcp_server.{h,cpp}，行分隔 JSON，非 LSP framing）
- `StdinReader::run()`（:604–647）`std::cin.getline` 4MiB 上限（`kMaxMcpLine`:611），
  超长 → sentinel → `-32700`（:700–704）；`sendResponse`（:1264–1290）compact + endl。
- `handleRequest`（:749–1236）：`initialize`（protocolVersion 固定 "2024-11-05"，
  capabilities.tools.listChanged=false，:771 instructions）、`notifications/initialized`、
  `ping`、`resources/list`（恒空 :817）、`prompts/list`（恒空 :823）、`tools/list`
  （:829–931，默认 compact 无 schema；`includeSchemas:true` 内嵌；limit/cursor 分页 :910–929）、
  `tools/call`（:933–1230 大 if/else 链）、`notifications/cancelled`（:780–806）。
  其余 → `-32601`；未 initialize → `-32002`（:740）。
- **进度：`sendNotification()`（:1340）零调用点 — 从不发 notifications/progress**；
  客户端只能轮询 `get_execution_status`。取消：rpc-id→taskId map（上限 1024，
  :1189–1201）→ `TaskCenter::cancelTask`（:797–803）。
- 沙箱 `SICNU_MCP_WORKSPACE`（validateWorkspacePaths）；custom_tools 门槛
  `SICNU_MCP_TRUST_CUSTOM_TOOLS`（isToolIdAllowed :1649–1680）；分页 clamp 1..500（:482–488）。
- 无服务端 timeout；同步单线程处理。宿主为桌面二进制 `sicnu_geo_rs --mcp`
  （src/app/main.cpp:165,314；stdin EOF → quit :693）。测试用 `TestMcpServer` 子类。

### CLI（src/cli/，二进制 sicnu_geo_rs_cli）
- Legacy flag 模式（main_cli.cpp:80–506）：`--pipeline/-p`、`--list/-l`（:281 RSOperatorRegistry
  仅 operator 名）、`--schema/-s`（:291 `op->schema()` 直接 dump）、`--export-catalog`（:305）、
  experiment 记录 + `--pin-*`（:126–168）。
- CLI 3.0 子命令（cli_commands.cpp dispatch :2255–2303）：algorithms（list/search/schema，
  **直读 AtomicAlgorithmRegistry，从不经过 AgentToolCatalog**，:147–230）、run、pipeline、
  workflow、plugin、models、project、data、data-providers、dataset/experiment/reproduce、
  lab、catalog export。全局 flags `--json/--json-lines/--quiet/--progress-json`（CliIO，
  cli_commands.h:30–46）。退出码 = `exprs::ExitCode`（src/sdk/exprs/exit_codes.h，0..7）。
- **CLI 无 spatial:/harness:/dataset:/meta 工具的 discovery** — 这些只在 MCP/Pi 可见。
- `lab_batch_runner.{h,cpp}`：D7 教学评分器（submissions 目录 → CSV），非通用 manifest batch。
- `rs_pipeline_runner.{h,cpp}`：单 pipeline JSON → TaskCenter::submitPipeline；resume。
- `sicnu_worker_main.cpp`：worker_protocol v1（newline JSON：run/cancel/shutdown →
  ready/progress/ack/result/error）— 纪律化 framed protocol 的在库先例
  （src/runtime/worker/worker_protocol.h:1–50）。
- Help projections（help_cli_projections.cpp:145–207）：`--operator-help/--help-topic/
  --list-topics/--export-help-docs`，与 GUI/MCP 共享 help registry。
- CLI 链接 sicnu_agent（经 sicnu_lab_batch PUBLIC 传递），可用 AgentToolCatalog。

### Pi（pi/）
- `exp-rs-spatial.ts`：spawn `<bin> --mcp`，`tools/list {includeSchemas:true}` →
  `pi.registerTool`（:174–221）；类别过滤 `EXP_RS_TOOL_CATEGORIES`（默认 meta,spatial,data,temporal,
  cartography,symbology,workflow,workspace,layout,harness，:83）；本地工具
  `exprs_wait_for_execution`（轮询+abort 取消，:111–172）、`exprs_status`；
  结果截断 50k chars 尾保留（:49–53）。
- `mcp_bridge.ts`：newline JSON-RPC client；REQUEST_TIMEOUT_MS=10min、STARTUP_TIMEOUT_MS=30s、
  MAX_LINE_BUFFER_CHARS=32MiB（:12–15）；fast-crash 断路器（5 次/10s，:311–318）；desync 检测
  → kill+respawn（:216–265）；abort → notifications/cancelled（:346–359）。
- `pi/test/*.mjs`：node:test，未接 CTest。

### TaskCenter / progress / cancel 基础设施
- TaskCenter（src/processing/framework/task_center.h）：含 WaitingResource/Cancelling 状态、
  `cancelTask(taskId, reason)`（:270）、`progressPercentage`（:120）、bounded logBuffer（:124）。
- 全库取消惯用法：`std::function<bool()> isCancelled` + progress callback
  （atomic_algorithm_adapter.h:28,48；RSOperatorContext::reportProgress/isCancelled；
  JobEngine :56–59；CLI `g_cliInterrupted` → `cliIsInterrupted()` cli_commands.h:53）。
- `src/runtime/observability/execution_telemetry.h`：有界环 + dumpJson — 仅诊断。
- workflow 进度仅 `get_workflow_status` 轮询。

### Help / diagnostics / drift gate 现状
- src/help/（help_composition/help_registry/help_markdown_writer/operator_help_provider 从
  live schema 派生 ParameterFact）；data/help/{commands,concepts,diagnostics,workbenches}.json。
- Gates：test_help_coverage（8 cases，含 help 文本无机器路径/凭据形状 gate :309）、
  test_capability_drift、test_algorithm_meta_drift（sidecar↔descriptor #707）。
- **kMetaTools[]/dataPlatformToolDefs() 无任何 parity gate**。

### 大结果处理现状
- MCP 结果内联 JSON（cpp:1203–1209）；缓解：512KiB compaction（spatial_tool.cpp:98–187）、
  metadata 分页（contracts::paginate）、Pi 50k 截断、MCP list clamp ≤500。
- **无 artifact 读取 API**（无 offset/range/cursor 检索）；artifact 仅作为
  ExpectedArtifact 声明与 file path + assetId。

### 测试基建
- Catch2（tests/sicnu_test_main.cpp）；`sicnu_add_test`（tests/CMakeLists.txt:57）；
  相关目标：test_mcp_server（:1661，直接编译 mcp_server.cpp）、test_agent_tool_catalog（:1701）、
  test_cli_commands_json（:8149，popen 真实 CLI）、test_generic_cli_manifest（:2703）、
  test_help_coverage（:8376）、test_toolbox_coverage（:2685）、test_tool_call_dispatcher（:416）、
  test_data_platform_surface（:2391）、test_harness_catalog（:8211）、test_lab_batch（:8760）。
- 唯一 golden E2E：test_agent_golden_workflow.cpp:39（MCP handler seam，非真 stdio）。

### CMakePresets.json
- configure presets：`dev-default`（build-dev, Debug, tests ON）、`ci-fast`、`ci-full`、
  `sanitizer-debug`、`release-package`；对应 build/test presets。

## 排名缺口 → Work package 映射（子代理结论，主代理确认）

1. 五 schema 源无统一投影/parity gate；CLI 从不见 catalog 全集 → **WP A/F**
2. MCP 无 progress 通知；取消仅轮询 → **WP C**
3. MCP session 浅健壮（版本固定、无 listChanged、无超时、同步语义未文档化）→ **WP D**
4. CLI 无通用 manifest batch（lab_batch 是评分器、pipeline 单文件）→ **WP B**
5. 大结果仅 compaction+metadata 分页，无 artifact 读 handle → **WP E**
6. kMetaTools/dataPlatform/Pi 类别表无 drift gate → **WP F**
7. redaction 仅 help 文本 gate，MCP/CLI 结果/日志无凭据/路径清洗 → **WP G**
8. 无真 stdio binary E2E（framing/超长行/取消/重复两次）→ **WP H**
9. 交互工具 headless 过滤启发式（view:get_state 探针）的 parity 陷阱 → 记录 + gate
10. legacy vs CLI 3.0 双 parser/schema 打印两种格式 → 记录（不重写 legacy）
