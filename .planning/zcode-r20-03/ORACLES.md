# R20-03 ORACLES — 验收 oracle 与实际证据索引

对应任务书 §8 四条 oracle + 通用边界回归。证据本体在 TEST_RESULTS.md / 本文件链接的命令输出。

## O1 — Pi→MCP→discovery→调用→工作流→status 全链真实执行
- pi Node 套件 39/39（含 mcp_bridge 故障注入：id_null、timeout_cancels、fake server 代际/EOF/半帧/runaway）——master GREEN 基线 ×1（本轮）；C++ 侧 test_mcp_server / test_surface_e2e / test_workflow_* 构建后 ×2。
- 取消/超时/重放 contract：test_r14_C_mcp_workflow_cancel、test_r19_02_workflow_cancel_order、test_workflow_cancel（构建后 ×2）。

## O2 — 权威状态不依赖残留/错误 schema/未授权审批；取消后无晚到成功
- #1539/#1540 live 已修（复拒绝裸布尔+令牌路径；dock worker 线程 arm）；W5 清死字段；test_agent_ops_core / task_center_executor ×2 回归。
- W1 使不可信 worker 帧无法以非对象 result 进入宿主消费路径（resolveReference UB 防线）。

## O3 — test_surface_snapshot 三腿一致或如实 KNOWN_BASELINE_FAIL
- CT-6（7 cartography 工具）04 轨未修期间：registry 腿预期失败，失败信息含工具名；projection/wire 腿必须绿。不修改 expected/共享源绕过。（构建后采集）

## O4 — 不另造 agent 框架
- 零新框架/新依赖：全部改动是对既有模块的类型门/诊断/死代码清理（git diff 可证）。

## 通用边界矩阵（适用项）
| 边界 | 覆盖 |
|---|---|
| 错误类型输入 | W1（非对象 result 帧）、W2（错型 sidecar 字段）、W3（错型工具参数）|
| 巨大/恶意输入 | W1 fuzz 既有套件回归；#1552 深嵌套已有界（journal stackLimit=128）|
| 协议违约诊断诚实性 | W4（TooLarge hexdump 恒空 → 真实前缀字节）|
| 取消/超时 | O1 既有族回归 |
| 进程崩溃恢复 | W8 plugin_host 进程族（R19-02 NOT_EXECUTED 债）|
