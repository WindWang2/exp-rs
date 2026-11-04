# GOAL — R25-C

## 使命
在现有 Pi/MCP 空间智能 Harness、Workflow 执行与插件工具契约上做纵深强化：让崩溃/重启后的桥接层与执行层**诚实、可诊断、可恢复**，不新建平行框架。

## 核心 Oracle（对应任务书 §7）
- **O1（W1）Pi 桥接生命周期诚实化**：initialize 响应的 serverInfo 被记录并暴露；respawn 后工具目录/版本漂移可检测（status 工具可见 + 壳层可感知）；bridge 级失败带类型码（CHILD_EXITED/RESTART_FAILED/REQUEST_TIMEOUT/STREAM_DESYNC/BRIDGE_STOPPED/PAYLOAD_TOO_LARGE）。测试：pi/test 新契约用例（假 MCP server 崩溃+目录变更场景）两轮 GREEN。
- **O2（W3）Workflow 恢复诊断**：get_workflow_status 支持 run_id 路径（服务器重启后仍可查），暴露 checkpoint 状态/可恢复性/中断步诊断；可列举可恢复 runs。真实 WorkflowRunCoordinator 路径，RED→GREEN×2，输入 schema 变更走 surface snapshot 再生。
- **O3（W2）能力诚实化**：list/search_algorithms 条目携带 prerequisites/limitations/deterministic/idempotent/supportsCancellation（descriptor 真值，非虚构）；断言至少一个已知算法的前置条件可见。
- **O4（W4）回归钉维持**：r22_agent 5 项 + 相关 workflow 回归全绿（不重复修复，只维持+验证）。
- **O5（W7）E2E 链**：真实二进制 --mcp 至少 4 条链：成功执行、类型化拒绝、取消、崩溃后 resume（借 O2 的诊断面）。真实 run_id、typed diagnostics。
- **O6（W8）R1/R2 独立评审 + PR**：两个独立上下文评审、findings 闭环、PR 推送创建。

## 明确不做
- 不重复 #1714/#1720/#1721 已修缺陷；不新建第二套 bridge/工具系统；不动 D 轨制图文件；不加无实现空工具；不改 A/B/E 轨所有权文件。
