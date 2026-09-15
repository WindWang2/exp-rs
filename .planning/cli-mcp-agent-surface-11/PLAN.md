# PLAN — cli-mcp-agent-surface-11

核心主张：**不建第二真值**。AgentToolCatalog 已是聚合层（algorithm/interaction/data/spatial 四
provider），MCP tools/list 已是 catalog ∪ meta ∪ dataPlatform 的并集——缺的是：(1) 把这个并集
收敛为一个**可被 CLI/MCP/help/测试共同消费的显式投影 seam**；(2) 把批处理/进度/取消/大结果/
redaction 变成三个 surface 上的同一契约。所有新代码落在 primary write scope。

## 新增 authority seam（src/agent/tool_catalog/，本 track 独占）

- `surface_registry.{h,cpp}` — `SurfaceToolDescriptor{name, family, title, description,
  inputSchema, outputSchema, source}` + `collectSurfaceTools()`：
  唯一并集投影 = AgentToolCatalog.listTools() + MetaProtocolTools + DataPlatformToolDefs。
  Meta 表从 mcp_server.cpp 抽到 `meta_protocol_tools.{h,cpp}`（行为不变的搬运，mcp_server
  改为消费头文件），使 CLI/测试可链接同一真值。
- `surface_redaction.{h,cpp}` — 凭据/秘钥形状文本清洗（bearer、api_key=、password=、PEM、
  Windows/Unix 绝对 home 路径可选模式），应用在 MCP result/log 边界与 CLI batch result index。
- `surface_progress.{h,cpp}` — 结构化进度事件信封 `{type, taskId, progress, total, message,
  state}`：TaskCenter 进度 → MCP `notifications/progress`（带 progressToken 回显）与 CLI
  `--progress-json` NDJSON 共用同一词汇表。

## Work packages → phases

| WP | 内容 | Phase | 主要文件（全部在 write scope 内） |
|---|---|---|---|
| A | surface census：collectSurfaceTools() 投影 + CAPABILITY_MATRIX.md 落盘 | 1 | src/agent/tool_catalog/surface_registry.*, meta_protocol_tools.*, mcp_server.cpp 消费改造 |
| F | schema parity：CLI 新 `tools` 子命令（list/schema，读同一投影）；test_surface_parity drift gate（MCP tools/list == 投影 == CLI tools list；Pi 类别前缀 ⊆ 投影 family 集合；meta/dataPlatform 表 schema 可解析且 dispatch 可达） | 1–2 | src/cli/cli_tool_commands.*, mcp_server.cpp, tests/test_surface_parity.cpp |
| B | CLI batch manifest：`batch run <manifest.json|jsonl>`：tasks[{id,operator|tool,params|paramsFile}]、${var} 插值、fail-fast/continue、NDJSON result index、exprs::ExitCode 分类、SIGINT → Cancelled + 剩余 skipped | 2 | src/cli/cli_batch_runner.{h,cpp}, cli_commands.cpp dispatch 接线, tests/test_cli_batch_manifest.cpp |
| C | progress/cancel：TaskCenter progress 回调 → surface_progress 信封 → MCP notifications/progress（_meta.progressToken 回显，有界速率）；终态唯一性断言；CLI batch --progress-json | 3 | src/agent/tool_catalog/surface_progress.*, mcp_server.cpp, tests |
| D | MCP robustness：initialize 版本协商（client supported → echo，else pinned）、超长行/坏 JSON/未知方法/未知 id cancel 的负路径测试、cancel map 已知语义文档化、id 类型保真 | 3–4 | mcp_server.cpp, tests/test_mcp_server.cpp 或新 test_surface_protocol.cpp |
| E | large result handles：meta 工具 `artifact_read {path, offset, length, maxBytes}`（workspace 沙箱 + 单次读上限 + nextOffset cursor + sha256）；>512KiB 结果的 bounded envelope 附 overflow 指引字段 | 4 | surface_registry 或 mcp_server.cpp 内 meta 工具新增， tests |
| G | redaction：redact() 应用于 MCP 工具错误/日志输出边界 + batch result index；known-answer tests（bearer/key/pem 形状） | 4 | surface_redaction.*, mcp_server.cpp, cli_batch_runner.cpp, tests |
| H | protocol E2E：真 stdio host——测试专用最小 host 可执行（复用 mcp_server 构造路径，stdin/stdout 真管道），场景 initialize→tools/list(includeSchemas)→tools/schema→tools/call→progress→cancel→artifact_read **连跑两遍** | 6 | tests/surface_mcp_host_main.cpp, tests/test_surface_e2e.cpp, tests/CMakeLists.txt append |

Phase 5 = 规模/故障硬化（bounded 队列/上限不变式），Phase 7 = 对抗 review（子代理 #2），
Phase 8 = 双验证 + rebase + PR。

## 明确不做（rescope）

- 不重写 legacy CLI flag parser（保持 verbatim；双格式缺口记录 CAPABILITY_MATRIX）。
- 不把 InteractionToolRegistry/SpatialToolRegistry 合并成新 registry（catalog 已聚合；
  只加投影）。不实现 MCP resources/prompts（master 返回空，保持并文档化）。
- 不动 `data_platform_tools.*` / `src/agent/spatial_tools/**`（open PR 领地；只读消费）。
- 不实现 server-side 任务超时强杀（TaskCenter 语义域，属执行运行时 PR #1009 领地）；
  以文档 + cancel 语义测试替代。
- 不修 issues #1001–#1007（域缺陷，零交集，OUT_OF_SCOPE）。

## 验证策略（Oracle 对应）

- Oracle-1（schema 一致）：test_surface_parity 三方相等断言（独立 oracle：从投影函数重建
  期望集，与 MCP/CLI 实际输出 diff）。
- Oracle-2（取消/终态唯一）：批内 cancel + MCP notifications/cancelled 的终态互斥断言。
- Oracle-3（payload cap）：构造 >512KiB 工具结果，断言 MCP 信封 ≤ cap 且 handle 可读回。
- Oracle-4（E2E×2）：test_surface_e2e 整场景函数级重复两次。
- Oracle-5：`git diff --check`、冲突标记/secret 扫描、drift gate 全绿。
- Oracle-6：Phase 8 关键 targeted 套件原样连跑两遍。
