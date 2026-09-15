# PERFORMANCE — cli-mcp-agent-surface-11

资源模型（本 track 全部为协议面代码，无栅格算法；资源语义 = 有界性不变式）：

- MCP 单行输入上限：4MiB（master 既有 kMaxMcpLine，保持）。
- MCP tools/list 分页 clamp：1..500（既有）。
- surface 投影内存：O(工具数)（当前全集 ~ 数百量级），每次调用重建，无缓存放大。
- progress 通知速率：有界（阈值触发 + 终态必发），防 NDJSON 洪泛；测量：protocol 测试断言
  通知数 ≤ 进度回调数（上界不变式，非 wall-clock）。
- artifact_read 单次读上限 256KiB；总响应受 512KiB 信封 cap 约束。
- batch result index：逐行流式写出，无全量驻留；队列上限 = manifest 任务数（输入有界）。
- cancel map 上限 1024（master 既有），本 track 不放大。
- 构建/测试资源：`-j2`/`-j1` 硬上限，记录见 EVIDENCE。
