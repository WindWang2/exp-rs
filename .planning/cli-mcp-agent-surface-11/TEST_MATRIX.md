# TEST_MATRIX — cli-mcp-agent-surface-11

每项能力 → 独立 oracle → 命令 → exit → evidence。随 Phase 推进填充实测结果。

| # | 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|---|
| T1 | surface 投影 = catalog ∪ meta ∪ dataPlatform 全集 | 投影函数独立重建期望集（provider 计数 + meta 表行数 + dataPlatform 行数），与 collectSurfaceTools() 输出 diff | `ctest -R test_surface_parity -j1` | 待测 | EVIDENCE P1 |
| T2 | MCP tools/list == 投影（名字+schema 逐字段） | TestMcpServer in-process 请求 vs collectSurfaceTools() | 同上 | 待测 | EVIDENCE P1 |
| T3 | CLI tools list --json == 投影 | popen 真实 CLI 二进制，JSON diff | 同上 | 待测 | EVIDENCE P2 |
| T4 | Pi 类别前缀 ⊆ surface families | 读 pi/exp-rs-spatial.ts 文本提取（静态） | 同上 | 待测 | EVIDENCE P2 |
| T5 | batch manifest known-answer（3 任务 1 败 1 跳过） | golden NDJSON result index 比对 | `ctest -R test_cli_batch_manifest -j1` | 待测 | EVIDENCE P2 |
| T6 | batch fail-fast / continue / SIGINT-cancel 终态 | 注入失败任务 + 中断标志 | 同上 | 待测 | EVIDENCE P2 |
| T7 | MCP progress 通知（progressToken 回显、有界速率、终态必达） | TestMcpServer 捕获 notification 流 | `ctest -R test_surface_protocol -j1` | 待测 | EVIDENCE P3 |
| T8 | cancel 终态唯一（terminal 状态互斥、重复 cancel 幂等） | TaskCenter 状态机观测 | 同上 | 待测 | EVIDENCE P3 |
| T9 | initialize 版本协商 + 负路径（超长行/坏 JSON/未知方法/未知 id cancel） | 协议响应码断言 | 同上 | 待测 | EVIDENCE P4 |
| T10 | artifact_read：沙箱拒绝、256KiB chunk、nextOffset、sha256、text/base64 | 独立 hashlib/QCryptographicHash 对照 | 同上 | 待测 | EVIDENCE P4 |
| T11 | redaction known-answer（bearer/api_key/password/PEM） | 独立正则 oracle | `ctest -R test_surface_redaction`（或并入 protocol） | 待测 | EVIDENCE P4 |
| T12 | E2E 真 stdio：initialize→tools/list→schema→run→progress→cancel→artifact，×2 | surface_mcp_host 子进程 + 真管道 | `ctest -R test_surface_e2e -j1` | 待测 | EVIDENCE P6 |
| T13 | 既有回归：test_mcp_server / test_agent_tool_catalog / test_cli_commands_json / test_help_coverage | master 基线行为不变 | `ctest -R "test_mcp_server|test_agent_tool_catalog|test_cli_commands_json|test_help_coverage" -j1` | 待测 | EVIDENCE P5 |
| T14 | drift/生成物零 diff；`git diff --check` clean | git | Phase 8 | 待测 | EVIDENCE P8 |
