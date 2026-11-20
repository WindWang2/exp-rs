# PLAN — r35-t05-agent-runtime

主题：**Agent 运行链的请求身份与输入边界**（一个连贯 PR）。

## 工作包（本 PR 覆盖）

| WP | 内容 | 状态 |
|---|---|---|
| WP01 | fresh master 清账、open PR 保留区、调用链核对 | DONE（BASELINE/PATH_OWNERSHIP/DECISIONS） |
| WP02 | SSE 分片无关：onReadyRead 只对无换行尾部判 8MiB bound | 待实现 |
| WP11 | proxy 单调请求 id（8001/8003 → per-call 唯一），迟到响应不误投 | 待实现 |
| WP13 | 完整行 strict UTF-8；FakeSocket 夹具修复恢复 3 条回归 | 待实现 |
| WP14 | `_mount_shm_array` header/容量校验 + C++ create() checked multiply | 待实现 |
| WP15 | `_json_safe` ndarray 预算（不先 tolist） | 待实现 |
| 验证 | 双锁构建目标 + ctest（新 C++ 测试、llm_streaming、python 套件） | 待执行 |
| R1/R2 | 独立 reviewer 读 BASE..HEAD diff + 行为证据 | 待执行 |
| 交付 | push 分支 + PR + 账本收尾 | 待执行 |

## 未纳入本 PR（真实残差，非放弃）

- WP03 Native 累计 bytes 预算、WP04 dock 批量渲染：行为面大，独立闭环。
- WP06 Pi receipt readSmall、WP05 run 身份、WP07-10、WP16-19：本轮无已复现缺口优先级低于上述五项；PR 后如有余量按序推进。
- E2E-02/03/05 的真实 daemon/GUI 腿：本环境 AF_UNIX listener EPERM（ENVIRONMENT_BLOCKED），如实记录。

## Oracle（每项）

- WP02：同一合法多行批量（总量>8MiB，单行均<8MiB）在 transport 上全部解析、无 `oversized_line` 拒绝；敌意无换行流仍拒绝（既有回归保持）。
- WP11：请求 A 超时→迟到 A 响应→请求 B：B 只收 B 的 result；A 的迟到响应进 drain 队列被主循环丢弃；两个 caller 各恰好一次结果。
- WP13：完整行含 `0xff` → 不产生 U+FFFD 改写的合法路径/名称，行被跳过；合法中文跨 recv 三条回归保持。
- WP14：header dtype≠请求 dtype / 短 mapping / 超容量 → typed 拒绝且无句柄泄漏；一致时正常读值；C++ `create(2^30,2^30,4,Float32)` 返回 false 不分配。
- WP15：size×24>1MiB 的 ndarray 在 tolist 前被 typed 拒绝；小数组/标量兼容不变。
