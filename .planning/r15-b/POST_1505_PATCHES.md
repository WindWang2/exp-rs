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

## B. 未落地：range_cache.cpp —— #1505 T6 重定向拒绝后回落 /vsicurl/

现状（读码确认）：`serveOrFetch()` 的 `catch ( const GeoError & )` 把 `fetchRangeOnce` 抛出的 `PermissionDenied`（重定向守卫拒绝私网跳转）当成传输失败，
转去 `fallbackRead()`，而 fallback 走原始 `/vsicurl/`，会不经守卫地跟随同一个重定向。

建议补丁：

```diff
-      catch ( const GeoError & )
-      {
-        transportFailed = true;
-      }
+      catch ( const GeoError &error )
+      {
+        // A policy refusal is final: the fallback would follow the same
+        // redirect through raw /vsicurl/ without the guard.
+        if ( error.code() == ErrorCode::PermissionDenied )
+          refused = true;
+        else
+          transportFailed = true;
+      }
 ...
+      if ( refused )
+      {
+        CPLError( CE_Failure, CPLE_AppDefined, "vsirangecache: origin redirect refused by policy" );
+        return 0; // readThroughCache reports a short read truthfully
+      }
```

为什么没落地：确定性 RED 需要"公网 origin → 302 → 私网"的拓扑；本机测试服务器只能监听 127.0.0.1，而开启
`blockPrivateNetworks` 后 127.0.0.1 在 open 阶段就被拒，到不了重定向分支。可行 oracle 设计：给 `offline::isPrivateHost`
加测试注入的"视为公网"主机白名单（仅测试构建），或把重定向策略判定抽成可注入函数，再用 `http_range_server` 返回 302 到
`169.254.169.254`，断言 `fallbackReads` 计数为 0 且读返回 0 字节。需要 owner 同意加测试接缝。

## C. 未落地：range_cache.h —— #1505 T11 生产安装默认开启私网阻断

现状：`RangeCacheConfig::blockPrivateNetworks = false`，两条 CLI 路径用默认配置 `install()`。
建议：在 CLI 安装点（`src/cli/**`，非本轨所有权）显式设 `blockPrivateNetworks = true`，并提供 `--allow-private-network`
逃生开关；**不**改结构体默认值（大量测试依赖 127.0.0.1 测试服务器）。oracle：CLI 级测试断言安装后的
`RemoteRangeCache::config().blockPrivateNetworks == true`。属于策略变更 + 跨所有权，未做。

## D. 未落地：offline_gate.cpp —— #1505 T3 / #1485 DNS rebinding

现状：校验阶段 `getaddrinfo()` 的结果被释放，libcurl 再次独立解析，攻击者控制的域名可在两次解析间切换到私网地址。
建议：校验函数返回已验证的地址集合；传输层用 `CURLOPT_RESOLVE`（`host:port:addr1,addr2`）把连接钉到这组地址，
重定向的每一跳重复"解析→校验→钉住"；GDAL `/vsicurl/` 路径无法注入 `CURLOPT_RESOLVE`，需要经由
`GDAL_HTTP_*` 配置或改走自有 HTTP 层。oracle：本地 DNS stub（返回序列 public→127.0.0.1）+ 断言第二次连接目标仍为第一次校验过的地址。
跨 transport 的较大改动，本轨不做。
