# POST_1505_PATCHES（第 8 项）

> 背景：#1505 在本轨开始前已合并（BASE `46791bc6` 已包含）。因此第 8 项按"已合并→真正落地"执行：
> 能写出确定性 oracle 的线程在本轨直接修复（RED→GREEN）；无法在本机构造确定性 oracle、或属于策略/默认值变更的，
> 只在此给出具体补丁与 oracle 设计，**未落地**。逐条结论以 `tests/test_r15_B_post1505.cpp` 的实际运行结果为准（见 TEST_RESULTS.md）。

## A. 已在本轨落地（resource_uri.cpp / param_guard）

| 线程 | 问题 | 结论（pre-fix 树 `07e2554b` 实跑） | oracle |
|---|---|---|---|
| #1481 T6 | `file://LOCALHOST` 大小写 | FIXED_ON_MASTER：首跑即绿 | `1481-T6:` |
| #1481 T7 | 解码后 NUL | FIXED_ON_MASTER：首跑即绿 | `1481-T7:` |
| #1481 T8 | CheckedPath 打开 URI 原文 | FIXED_ON_MASTER：首跑即绿（`longPathSpelling` 由 `gdalTarget` 派生） | `1481-T8:` |
| #1481 T9 | 绝对 file URI 被当相对路径拼接 | FIXED_ON_MASTER：首跑即绿 | `1481-T9:` |
| #1481 T19 | `file:///`、`file:///C:/` canonical 丢根分隔符 | FIXED_ON_MASTER：首跑即绿（本轨起草的盘符根补丁因此撤回，未提交） | `1481-T19:` |
| #1505 T9 | 相对文件名里的字面 `%` 被当遍历拒绝 | REPRODUCED → FIXED_HERE：RED `resolveAgainst("/base","reports/100%_coverage.tif")` 为 Invalid；修复后只拒绝解码后为 NUL / `..` 的段（`%2e%2e`、`%252e%252e`、`%00` 仍拒绝） | `1505-T9:` |

## B. 已落地：range_cache.cpp —— #1505 T6 重定向/策略拒绝后回落 /vsicurl/（提交 `7ed6dc02`）

`serveOrFetch()` 现在把 `PermissionDenied` 视为终局：不再调用 `fallbackRead()`，读返回 0 并 CPLError。
确定性 oracle 不需要测试接缝：对已打开的句柄在读之前把 `blockPrivateNetworks` 打开，下一次未缓存读即被 `fetchRangeOnce` 拒绝——与重定向跳被拒走同一个 catch 分支。
RED（pre-fix）：读到 4096 字节、fallback_reads 0→1、origin 请求 2→5；GREEN×2。重定向跳本身没有单独 oracle（本机无法构造"公网 origin → 私网"拓扑），属于同一代码路径。

## C. 已落地：#1505 T11 生产默认开启私网阻断（用户 2026-10-17 批准写 src/cli/**，见 OWNERSHIP.md）
- `84cfbf27`：`data cache <url>`、`data cache prefetch`、`data identity` 默认阻断；`--allow-private-network` 为逐次调用的显式开关。test_cli_commands_json 的回环用例经局部 helper 显式放行，没有翻转全局默认，也没有逐个测试改开关。
- `53941117`（R3-10）：策略开启时，传输失败后的回落也走受守卫的传输。
- `127ac320`（R4-01）：策略同时覆盖探测、重新验证与 Stat。
- `08d8b295`（R4-02）：prefetch 先装受守卫缓存再规划；规划器的 identity 探测受守卫。
- 结构体默认值 `RangeCacheConfig::blockPrivateNetworks=false` 保持不变（库层与既有测试不受影响）。生产入口的默认值由 CLI 设定。
- 剩余的、range cache 之外的路径：REVIEW_ROUND4 R4-03（P2）。

## D. 已落地：offline_gate.cpp / http_fetch.cpp —— #1505 T3 / #1485 DNS rebinding（提交 `fc712c72`）

新增 `offline::resolvePinnedPublicAddresses()`：对受守卫的每一跳再解析一次主机名，逐个校验全部地址，任一私网地址或无法解析即拒绝（fail closed）；
通过后用 `CURLOPT_RESOLVE` 把这一跳钉到这组已验证地址，libcurl 不再自行查询 DNS。IP 字面量主机不受影响。
oracle：测试二进制内插 `getaddrinfo`（只对一个测试专用域名生效：第一次答 192.0.2.1，之后答 127.0.0.1）。
RED（pre-fix）：lookups=2，fixture 收到 1 个请求；GREEN×2：fixture 从未被连接。
剩余限制（记录）：走代理时由代理解析（钉不住）；不经守卫的 GDAL `/vsicurl/` 路径（例如传输失败后的 fallback）不在此修复范围。
