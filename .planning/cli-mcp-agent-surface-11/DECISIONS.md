# DECISIONS — cli-mcp-agent-surface-11

D-001 **统一投影落点 = src/agent/tool_catalog/**：候选 (a) 新建 src/agent/surface/ 顶层目录；
(b) 复用 tool_catalog 目录追加 surface_* 文件。选 (b)：goal write scope 明确
`src/agent/*tool_catalog*` 独占可写，且 catalog 本就是聚合层，投影属其自然延伸；避免新 CMake
目标、最小接线。拒绝 (a)。

D-002 **meta 工具表搬运而非重写**：`kMetaTools[]`/`metaToolInputSchema()` 从 mcp_server.cpp
原样搬到 `meta_protocol_tools.{h,cpp}`（改名 `MetaProtocolTools` 命名空间），mcp_server.cpp
改为 include。候选 (a) 在 mcp_server.cpp 保留并 export；(b) 搬运到 tool_catalog。
选 (b)：CLI 链接 sicnu_agent 而非 mcp_server.cpp（后者目前只被 test/app 编译单元引用），
不搬运则 CLI 无法消费同一真值；行为零变化，diff 可逐行核对。

D-003 **dataPlatformToolDefs 只读消费**：open PR #1009 已修改 data_platform_tools.cpp（+4 行
include）。本 track 不改该文件，投影函数直接调用其现有 `dataPlatformToolDefs()` 声明（头文件
已暴露）。风险：#1009 若再改 schema 表内容，parity gate 会自动捕获——正是 gate 的职责。

D-004 **CLI discovery 走新 `tools` 子命令而非改 `algorithms`**：`algorithms list/schema` 语义
锁定 Processing descriptors（历史契约，test_cli_commands_json 依赖）；新 `tools` 子命令投影
全 surface（= MCP tools/list 同源）。候选 (a) 扩展 algorithms；(b) 新子命令。选 (b)：零破坏、
语义清晰；CAPABILITY_MATRIX 记录两者关系。

D-005 **batch manifest 格式**：JSON（单对象 {version,variables,policy,tasks[]}）与 JSONL
（每行一个 task 记录，首行可选 policy）双形态；变量插值仅 `${var}` 形式（与 workflow
placeholder_grammar 的文件路径占位不冲突——batch 插值发生在 params 字符串值内，不解析
step 引用）。fail-fast 默认 **false**（continue），与 lab_batch_runner 的“continue past
throwing submissions”先例一致。退出码映射：全部 ok→0；任一任务失败→3（ExecutionFailure）；
manifest/schema 非法→2（ValidationFailure）；SIGINT→4（Cancelled）；参数错→6；
runtime 不可用→7。result index 为 NDJSON（--result-index 路径或 stdout）。

D-006 **进度信封自建词汇表而非复用 worker_protocol**：worker_protocol v1 是 worker 宿主协议
（run/cancel 帧），语义不匹配 surface 事件流。surface_progress 定义最小信封
{type:"progress"|"state", taskId, progress, total?, message?, state}，MCP progress 通知映射
MCP spec 字段（progressToken/progress/total/message），CLI --progress-json 直出。无双真值：
两者都是同一 TaskCenter 进度源的投影。

D-007 **MCP progress 通过 _meta.progressToken 触发**（MCP 2024-11-05 规范行为）：客户端在
tools/call 请求 `_meta.progressToken` 提供时才订阅并发射 notifications/progress；无 token
时行为与 master 完全一致（不发通知）。有界速率：每次进度回调 diff 达阈值才发（防止
NDJSON 洪泛），最终 state 变化必发。

D-008 **initialize 版本协商**：请求 protocolVersion ∈ {受支持集} → echo；否则响应我方最新
支持版本（当前 "2024-11-05"）。候选 (a) 永远回 pinned（现状）；(b) 最小协商。选 (b)：MCP
规范要求，且向后兼容（老客户端发 "2024-11-05" 得到 echo）。

D-009 **artifact_read 为 meta 工具**（工具名 `artifact_read`，meta 家族）：读文件切片
{path, offset=0, length≤kMaxArtifactChunk=256KiB, encoding=text|base64}；强制 workspace
沙箱（与 validateWorkspacePaths 同规则）；返回 {path,sizeBytes,offset,nextOffset,
truncated,sha256,content}。候选 (a) 扩展每个空间工具支持 handle；(b) 单一读取 meta 工具。
选 (b)：一处实现、全工具受益、契约可独立测试。sha256 用 QCryptographicHash（Qt 已依赖）。

D-010 **redaction 默认开、无开关**：redact() 是形状匹配（bearer/api_key=/password=/PEM/
Authorization:）+ 可选 home 路径压缩；应用于 MCP 工具 result 的 message/log 字段与 CLI
batch result index。不 redact 文件**内容**（artifact_read 返回的数据是业务数据，使用方
显式请求读取），只 redact 协议面文本。误报方向宁可多杀（false positive 可容忍）。

D-011 **E2E 用测试专用 host 可执行而非 sicnu_geo_rs --mcp**：桌面二进制拉起 GUI 栈（QGIS
init、resource 搜索路径），offscreen 下仍重且脆。tests/surface_mcp_host_main.cpp 复用
mcp_server.cpp 同一构造序列（与 test_mcp_server 相同的编译单元），绑定真 stdin/stdout，
由 test_surface_e2e 以子进程 spawn——真 stdio framing、真子进程生命周期、hermetic。
真实 sicnu_geo_rs --mcp 手动验证记录于 EVIDENCE（不作为 gate）。

D-012 **Pi parity gate 是静态结构检查**：读 pi/exp-rs-spatial.ts 文本，提取
EXP_RS_TOOL_CATEGORIES 默认前缀集，断言 ⊆ surface family 集合（投影导出的 namespace 并集）。
不执行 TS。C++ 测试以源码相对路径定位 pi/（复用 test 侧已有的 source-dir 宏，若无则
CMake target_compile_definition 注入）。

D-013 **不实现 notifications/tools/list_changed**：catalog 运行时突变仅发生在 custom_tools
安装（trust-gated），频率极低；翻转 capability 位会引入通知时序契约。保持 listChanged=false
并在 docs 明示“客户端应在每次会话重新 tools/list”。记录 follow-up。

D-014 **CHANGELOG/.gitignore/tests/CMakeLists.txt 最后 integration commit**：三处均为 open
PR #1008/#1009 触碰的共享文件；append-only、最小 diff、推迟到 Phase 8 前一次性提交，
降低 rebase 冲突面。
