# REVIEW_ROUND4（第四轮自审，2026-10-17 UTC+8）

范围：`84cfbf27`（T11 CLI 默认阻断）、`53941117`（R3-10）、`127ac320`/`08d8b295`（R4-01、R4-02）。同一 agent 的等价自审，**不是独立评审**。
方法：
1. 逐个检查生产入口：所有 `RemoteRangeCache::install` 调用点、所有 `RemoteSourceValidator::probe` 和 `remoteIdentityToken` 调用方。
2. 用真实 CLI 子进程加回环 fixture 计数请求。
3. 用 getaddrinfo 插桩重现 DNS rebinding。

## 发现
| ID | 级别 | 描述 | 状态 |
|---|---|---|---|
| R4-00 | P1 | #1505 T11：`data cache <url>`、`data cache prefetch` 以 `RangeCacheConfig{}`（不阻断）安装缓存；`data identity` 以不阻断的 validator 探测。RED：cache 退出码 0、fixture 收到 2 次请求；identity 退出码 0、1 次请求。 | 已修 `84cfbf27`：默认阻断，提供逐次调用的显式开关 `--allow-private-network`；test_cli_commands_json 的回环用例经局部 helper 显式放行 |
| R3-10 | P1（自 P2 上调：是绕过守卫的真实路径） | 受守卫的 ranged fetch 传输失败后回落到原始 `/vsicurl/`，后者重新解析、不校验、不钉住。RED：重绑定后 fixture 收到 3 次请求，读出 4096 字节。 | 已修 `53941117`：策略开启时 http 回落走同一受守卫、钉住的传输；ensureSize 不打开原始 /vsicurl/ |
| R4-01 | P1 | **T11 的 prefetch oracle 发现的新问题**：`validatorOptions()` 丢掉了 `blockPrivateNetworks`，导致策略开启时，open 时的身份探测、RevalidateOnOpen 和 Stat 仍是无守卫、无钉住的请求。RED：对回环 URL 调用 `VSIStatL` 返回 0，fixture 收到 1 次请求；重绑定名 open 返回句柄，fixture 收到 1 次请求。 | 已修 `127ac320`：标志传到 validator；拒绝 = 静默 miss（不让异常穿过 GDAL handler 边界）；Stat 先做主机检查；取 size 不再走原始 /vsicurl/ |
| R4-02 | P1 | prefetch 在安装缓存**之前**做规划：chunk 规划经 `fabricCachedPath()`（此时 = 原始 /vsicurl/）打开首个资产；identity_cache 阶段用 `AssetIdentityOptions{}` 探测。R4-01 修复后仍有 2 次请求。 | 已修 `08d8b295`：先装受守卫缓存再规划；`FabricPlanOptions`/`AssetIdentityOptions`/`RemoteIdentityTokenOptions` 新增 `blockPrivateNetworks`（默认 false，拒绝 = 不可证明的空 token）；CLI 默认开启。fixture 请求数 6 → 2 → 0；opt-in 正对照仍能到达 fixture |
| R4-03 | P2（本人判断，见下） | range cache **之外**的远端路径仍沿用库默认（不阻断）：`data inspect/doctor/probe/window/mirror materialize <http-url>` 经 GDAL 原始 `/vsicurl/` 读取；`remoteIdentityToken` / `data_locality` / `data_doctor` 的默认选项；`src/operators/io/io_fabric_operators.cpp` 中的 `planFabric(intent, {})`（只读所有权）。 | LIVE_DEFERRED |
| R4-04 | P3 | `data identity` 遇私网主机时从"exit 0 + state offline"改为"exit 6 + permission_denied"。属于有意的行为变更，可用 `--allow-private-network` 恢复。 | 已记录 |
| R4-05 | P3 | `data cache status` 不安装缓存，因此报告的是库默认配置（`block_private_networks=false`），而不是读路径的 CLI 默认值。 | 已记录 |
| R4-06 | P3 | 策略开启时，http 降级读不再有原始 /vsicurl/ 的"正确性兜底"（例如超大且忽略 Range 的源）。结果是如实的短读或失败，不是错误数据。 | 已记录 |
| R4-07 | P3 | prefetch 的规划阶段现在经过缓存，首个资产头部可能在 `telemetry_before` 之前就已缓存。 | 已记录 |

### R4-03 定级理由（供用户复核）
- #1505 T11 的对象是"range cache 的生产默认"。该入口（以及同一命令的规划阶段、缓存内的探测/Stat/回落）已全部闭合，并都有 RED→GREEN。
- R4-03 是 master 上既有的、更宽的架构属性，不经过 range cache。要修，需要做进程级 GDAL 网络策略，或把库默认整体翻转（会牵动几十个使用回环 fixture 的测试和 src/operators/**），属于跨轨策略决定。
- 如果用户把它视为 P1，应把 PR 退回 Draft（`gh pr ready 1513 --undo`）。

## 统一远端闸门
没有放宽任何检查：所有改动都只是增加拒绝点，或把已有守卫传到漏掉的路径上。对象存储（VSI）条目的签名回落不受影响。

## 结论
没有未关闭的 P0/P1（R4-03 定为 P2，理由见上）。
