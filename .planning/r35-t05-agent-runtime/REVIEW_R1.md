# REVIEW_R1 — r35-t05-agent-runtime

- Reviewer：独立 general-purpose subagent（未参与实现），审查范围 `120677e6..7f9ac4de93`（4 commits）。
- 方法：读全量 diff + 周边生产代码与调用方（app_interface_bridge/python_ipc_server 的 ACK 行为、wire 常量）+ 亲自执行三组行为测试（13+3+4 全过）。
- 结论：REQUEST_CHANGES（1×P1、1×P2、3×P3）。

## Findings 与处置

| # | Severity | 内容 | 处置 |
|---|---|---|---|
| 1 | **P1** | `_proxy_request_seq=8000` 起的序列会铸造 8002/8004/8005——恰为 fire-and-forget 固定 id，而 host 对**每个** daemon 请求回 ACK（echo id）；相关调用铸到这些数字时会把待处理 ACK 当作自己的答案，无需超时即可误投（reviewer 实证复现）。旧固定 8001/8003 与这些 id 本不相交——该窗口为本次改动引入。 | **已修**：序列改为 10000 起（高于 9999 menu id），更正注释；新增回归 `test_pending_fire_and_forget_ack_is_not_served_to_a_correlated_call`。commit `d0d1353435`。 |
| 2 | **P2** | 32B/元素统一预算拒绝 >32768 元素的一切数组：200×200 uint8 掩膜（~200KB JSON，原可正常交付）被 typed 拒绝——真实行为回退。32B 对数值 dtype 是可靠上界（无超限漏网）但过保守。 | **已修**：dtype 感知估计（整数=np.iinfo 位数+2，浮点=26+2，未知 32）；新增回归 `test_compact_dtype_masks_still_deliver`；float64 重数组仍在 tolist 前拒绝。commit `d0d1353435`。 |
| 3 | P3 | 预算非 `<U`/object dtype 或嵌套容器数组的上界——但 1MiB 编码检查（wire 权威）仍捕获，typed -32001，内存行为与改前相同。 | 接受（与改前等价，wire 检查为权威）。 |
| 4 | P3 | 匹配到的 JSON-RPC error 响应被静默返回 `{}`/`None`（`obj.get("result", {})` 丢 error body）——**既有行为，非本 diff 引入**，但 WP11 恰好触碰这些相关位点。 | 记 HANDOFF 后续项；不在本 PR 扩围。 |
| 5 | P3 | .gitignore 白名单已提交但 planning docs 尚未提交（当时未到收尾步）。 | 收尾时随最终 commit 提交。 |

## Reviewer 已验证无问题的声明

WP02（尾部分离、无双 terminal、reentrancy 完整、seam 无状态破坏）；WP11 其余子声明（host→daemon 请求不可被当作响应、迟到行由主循环丢弃、无回绕）；WP13（三处解码点全转换、合法多字节跨 recv 契约保持）；WP14 Python（校验全覆盖、fd 无泄漏、struct 布局与 C++ Header 精确一致、在树内发送方 header 一致无误拒）；WP14 C++（除法安全、与 attach() 拼写一致、全部调用方合法维度）；WP15（常量与 host wire 限制一致、ValueError→typed -32000 非 -32700）；交叉项（无新线程、Qt 生命周期、IPC wire 不变、注册/白名单 append-only）。

## 修复后验证（R1 fix round）

- `python3 tests/test_r35_t05_worker_daemon_identity.py` → **15/15 PASS exit 0**（13→15，含 2 条 R1 回归）
- `python3 tests/test_worker_daemon_proxy.py` → **3/3 PASS exit 0**（id 断言随 10000 基线更新）
- `ctest -R "...30 项正则..."` → **100% tests passed out of 30**
