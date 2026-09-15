# CLI, MCP & Agent Surface Convergence 11.0

> **Local evidence only; no online CI dependency.**

## Baseline & dedupe

- Baseline `origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa`（启动时唯一相关 open PR：
  #1008 spectral、#1009 execution-runtime；#991/#992 已在基线内合并）。
- **文件级 ownership**（详见 PARALLEL_OWNERSHIP.md）：本 track 业务主体全部落在
  `src/agent/tool_catalog/**`、`src/agent/mcp_server.*`、`src/cli/**`、`pi/**`（仅文档）、
  `tests/test_surface*|test_cli_batch*`、`docs/agents/**` —— 与 #1008（spectral 域 + spatial_tools 注册）
  和 #1009（src/runtime/** 执行域）零业务重叠。共享 integration 文件（tests/CMakeLists.txt、
  src/agent|cli/CMakeLists.txt、CHANGELOG）append-only 最小 diff。
- `data_platform_tools.*` 未修改（#1009 触碰过）：本 track 只读消费其现有头文件表。
- Open issues #1001–#1007 均为 io/workflow/dataset/georef 域 R2 残留，与本 surface track 零交集，
  登记 OUT_OF_SCOPE 未修。

## Why（现状缺口，全部有 文件:行号 证据）

10.0 之后的 agent/CLI/MCP surface 有 **五个并行 schema 源**（AgentToolCatalog、
mcp_server.cpp 内 kMetaTools 手写表、dataPlatformToolDefs 手写表、InteractionToolRegistry、
AlgorithmDescriptor），且：

1. MCP `tools/list` 是唯一全集中消费者；**CLI 从不见 catalog 全集**（只读 Processing descriptors），
   `get_tool_schema` 对 meta/dataPlatform 工具甚至回答 Unknown tool；
2. **MCP 从不发 notifications/progress**（sendNotification 零调用点），客户端只能轮询；
3. CLI 无通用 manifest batch（lab_batch 是教学评分器；pipeline 单文件）；
4. 大结果只有 512KiB compaction + metadata 分页，**无 artifact 读取契约**；
5. kMetaTools/dataPlatform/Pi 类别表无任何 drift gate；
6. 协议面无凭据清洗（唯一 gate 只覆盖 help 文本）；无真 stdio E2E（唯一 golden 走 handler seam）。

## 架构决定（详见 DECISIONS.md D-001..D-017）

1. **不建第二真值**：`collectSurfaceTools()`（tool_catalog/surface_registry）= catalog ∪ meta ∪
   dataPlatform 的唯一并集投影；MCP tools/list、CLI `tools` 命令、get_tool_schema 回退全部渲染它。
   meta 表从 mcp_server.cpp **原样搬运**到 meta_protocol_tools.{h,cpp}（CLI 可链接同一真值）；
   allow-prefix 策略同搬运（surfaceIdAllowed）。
2. 进度：TaskCenter（既有 authority）→ `_meta.progressToken` 订阅 → notifications/progress
   （MCP 2024-11-05 三字段；5 点阈值 + 终态唯一发射；无 token 零通知，与 master 行为一致）。
   CLI 走既有 --progress-json NDJSON。无双真值：同一 TaskCenter 源的两个投影。
3. artifact_read：单一 meta 工具读文件切片（一处实现全 surface 受益），256KiB/片 + 整文件
   sha256 + nextOffset 游标 + 沙箱（resolved-path 检查，`../` 逃逸被拒）+ UTF-8 拒绝→base64。
4. redaction：形状匹配（bearer/PEM/key= 赋值/连接串口令），应用于 MCP 错误结果与 batch
   result index；路径明确不清洗（业务数据，暴露由沙箱治理）。
5. batch manifest：严格解析（未知 key 即契约违规，防 typo 静默默认）、fail-fast/continue、
   SIGINT→Cancelled+tail skipped（exit 4）、原子 tmp+rename result index、exprs::ExitCode
   最劣值聚合。

## 实际交付（WP A–H）

- **A/F** 统一投影 + CLI `tools list|search|schema` + 三方 parity gate
  （test_surface_parity：投影不变式、MCP tools/list == 投影、meta/dataPlatform 表全部
  dispatch 可达、spatial dispatch 家族 ⊆ listing 策略、Pi stale-category gate、CLI 真实二进制
  三方 parity、2000 工具线性规模 + 分页 500 clamp）。
- **B** `batch run|validate` + sicnu_cli_batch 静态库（test_cli_batch_manifest：12 类负样本、
  插值、策略、取消、原子索引、redaction）。
- **C** notifications/progress 中继（test_surface_protocol：token 回显、[0,1] 界、恰一终态、
  无 token 零通知）+ 终态唯一性（重复 cancel 幂等、终态后状态恒定）。
- **D** 协议负路径（超长行→-32700 且连接可用、坏 JSON、-32601/-32602 区分、-32002 未初始化
  ——后者 master 已有，补测）、initialize 版本协商负样本。
- **E** artifact_read（known-answer sha256/base64 独立常数、chunk clamp、游标走尾、目录/
  缺失/越界负样本、非 UTF-8 拒绝）。
- **G** redaction known-answer（test_surface_protocol）+ 边界应用。
- **H** 真 stdio E2E：surface_mcp_host（真子进程真管道，同 mcp_server.cpp）→
  initialize→tools/list→schema→run→progress→cancel→artifact→malformed，**整场景 ×2**
  （test_surface_e2e）。

## 兼容性

- 旧 API 零破坏：meta 表逐字节搬运；tools/list 线序不变（meta→dataPlatform→catalog，同一
  过滤规则）；新增唯一 meta 工具 artifact_read 追加在表尾。未初始化/未知方法/未知工具的
  错误码行为不变。无 token 的 tools/call 与 master 行为逐字段一致。
- CLI 新增 `tools`/`batch` 子命令，不改既有命令；legacy flag 模式未触碰。

## Local tests（本机 Linux/Make dev-default，QT_QPA_PLATFORM=offscreen，ctest -j1）

（结果在 Phase 8 双验证后回填；每条映射 命令→exit→TEST_MATRIX 行。）

| 套件 | 覆盖 | exit |
|---|---|---|
| test_surface_parity | 9 cases（投影/三方 parity/Pi gate/scale） | 待回填 |
| test_surface_protocol | 11 cases（progress relay/artifact/redaction/协议负路径） | 待回填 |
| test_cli_batch_manifest | 10 cases | 待回填 |
| test_surface_e2e | 真 stdio ×2 | 待回填 |
| test_mcp_server（回归） | master 基线 | 待回填 |
| test_agent_tool_catalog / test_cli_commands_json / test_help_coverage（回归） | master 基线 | 待回填 |

## Known limitations / follow-ups

- MCP resources/prompts 恒空、listChanged=false 保持（客户端约定：会话开始重新 tools/list；
  docs 已注明）。
- 服务端任务超时强杀属执行运行时域（#1009 领地），本 track 交付 cancel 语义 + 文档。
- Pi 未采纳 progressToken（现有轮询仍工作）；mcp_bridge.ts 逻辑未改。
- legacy CLI 双 parser/双 schema 格式保留（不重写，CAPABILITY_MATRIX 记录）。
- Pi 的 EXP_RS_TOOL_CATEGORIES 默认表刻意不含 rs:/gdal: 等家族（LLM 上下文策展）；
  gate 只防 stale 引用，不强制完备。

## Resource 证据

- 构建 -j2 硬上限（与并行 track 共享宿主时亦然）；60s 采样 load/RSS 见 EVIDENCE；
  configure 以 SICNU_LAB_SKIP_PYTHON_BINDINGS=ON + 本地 FETCHCONTENT 源完成（离线教学机
  路径，零 CMakeLists 修改）。
