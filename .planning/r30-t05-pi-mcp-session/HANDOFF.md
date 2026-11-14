# R30-T05 HANDOFF — 跨轨交接

## → T04（执行/工作流/产物）：取消信号契约的可观察性（已在本轨 fixture 层修复，引擎层无需动作，仅知会）

**现象（最小复现）**：`tests/surface_mcp_host` 旧 fixture 的 `rs:surface_sleep` 在 `ctx.isCancelled()` 时**返回** `{cancelled:true}` 载荷而非按契约 `ctx.throwIfCancelled()`（`atomic_algorithm_adapter.cpp:349-358` + `rs_operator_context.h:109-111` 是文档化契约：抛 `RSOperatorError(ErrorCode::Cancelled)`）。结果：取消确实到达引擎（status 可观察 `cancelling`），任务却以 `completed` 终结——`pi/test/r25_c_e2e_real_host.test.mjs` 的 cancel 段因此失败（该测试在无 host 二进制的环境一直 skip，master 上从未在本机跑过）。

**本轨处置**：修 fixture（`tests/surface_mcp_host_main.cpp` SleepOperator 改 `throwIfCancelled`）——测试基础设施，属 T05 所有权。引擎代码未动。

**给 T04 的信息**：如果你们在真实算子/JobEngine 侧观察到"cancel 到达但终态 completed"类竞争，先检查算子是否按契约抛 Cancelled；`band_math.cpp` 等真实算子走的是 `executeWithCancellation` 的 false+errorMessage("Cancelled") 通道，RSOperator 系走 throw 通道。两条通道在 TaskCenter 的终态映射如果还有洞，归你们。

**证据**：`/tmp/r30t05_e2dbg.mjs`（复现脚本，取消后 status: cancelling → completed）；修复后 r25 cancel 段与本轨 E2 window-1 均以 `canceled` 终结（见 LEDGER）。

## → T04（次要）：无

## → T06/T07：无

## ← 来自其他轨：无（当前无阻塞本轨的交接）
