# CAPABILITY_MATRIX — cli-mcp-agent-surface-11

before = origin/master@a5b11b7f10；after = 本 track 完成态。✅=implemented ⚠️=degraded/partial ❌=not-supported

| 能力 | before | after | 说明 |
|---|---|---|---|
| 全 surface 工具统一投影（catalog∪meta∪dataPlatform） | ❌（仅 MCP 内联拼装） | ✅ collectSurfaceTools() | authority seam，单函数 |
| CLI 全 surface discovery（tools list/search/schema） | ❌（CLI 只见 Processing） | ✅ `tools` 子命令 | 与 MCP 同源 |
| 三方 schema parity gate（MCP/CLI/投影） | ❌ | ✅ test_surface_parity | 含 Pi 静态前缀 gate |
| CLI 通用 batch manifest（JSON/JSONL、变量、fail-fast/continue、result index、exit taxonomy） | ❌（lab_batch 是评分器） | ✅ `batch run` | exprs::ExitCode 全集 |
| MCP notifications/progress | ❌（sendNotification 零调用） | ✅ progressToken 订阅 + 有界速率 | 2024-11-05 规范 |
| 长任务取消（MCP） | ⚠️（notifications/cancelled→TaskCenter，无测试覆盖负路径） | ✅ + 终态唯一断言 | |
| CLI batch 取消/终态 | ❌ | ✅ SIGINT→Cancelled+skipped | |
| MCP initialize 版本协商 | ❌（固定回 pinned） | ✅ 最小协商 | 向后兼容 |
| artifact 读取 handle（offset/cursor/sha256） | ❌ | ✅ artifact_read meta 工具 | 256KiB chunk 上限 |
| 协议面 redaction | ❌（仅 help 文本 gate） | ✅ 形状匹配 + 边界应用 | false-positive 容忍 |
| 真 stdio E2E ×2 | ❌（唯一 golden 走 handler seam） | ✅ surface_mcp_host 子进程 | |
| MCP resources/prompts | ❌（恒空 stub） | ❌ 保持 + 文档化 | follow-up |
| notifications/tools/list_changed | ❌ | ❌ 保持 listChanged=false | D-013，follow-up |
| server-side 任务超时强杀 | ❌ | ❌（TaskCenter 语义域，#1009 领地） | 文档化 |
| legacy CLI 双 parser/schema 双格式 | ⚠️ | ⚠️ 保持（不重写 legacy） | 记录为 known limitation |
