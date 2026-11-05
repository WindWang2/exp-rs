# WP06 远程 COG / 缓存 / 离线统一入口 — 站点审计（2026-11-05 04:3x +0800）

权威 = `geo::remote::safeRemoteRasterPath`（scheme→offline→私网 deny-by-default→cache+policy 必须同时在场，全部 typed 拒绝，绝不降级裸 /vsicurl/）。既有覆盖（test_r27_t02_safe_remote 6 用例 + test_io_remote_range/probe/cache）：scheme/私网/离线/fail-closed/合法 https 映射/RemoteDatasetPool::acquire 路由。

## 域内 open 站点逐案结论

| 站点 | 行为 | 结论 |
|---|---|---|
| data/providers/remote_source_cache.cpp:139 | 经 safeRemoteRasterPath | OK（#1803 RIO-6） |
| **fabric/object_store.cpp fabricCachedPath(RemoteHttp)** | 修复前：cache 未装→**裸 URL 返回**（无 scheme/私网/离线门）；装了→cache 包裹 | **CONFIRMED_OPEN → 已修**（R28-T02：未装 cache 的 http 拼 typed GeoError(InvalidArgument) 拒绝；私网/离线权威保留给调用方安装的 RangeCacheConfig——显式 lab opt-in 语义不破坏；local/object-store 分支不变） |
| fabric/chunk_plan.cpp:233、prefetch.cpp:148/524、virtual_cube.cpp:414/693、mirror.cpp:1140 | 经 fabricCachedPath 打开 record.path（可为远程 COG） | 随上项修复获得 fail-closed；调用方职责（先装 cache）被强制 |
| cli/cli_commands.cpp CacheGuard(T11) | 先 install(cliRangeCacheConfig(allowPrivateNetwork)) 再 plan/prefetch | OK（显式 --allow-private-network 走配置权威） |
| geospatial/stac/stac_client.cpp selectCogHref | 只产出 "/vsicurl/"+href 拼写（含 validateAssetHref），不 open | 拼写层 OK；**消费方**=src/app 对话框（T04，#1765 GUI 侧未接线→HANDOFF）；canonical 链（STAC→catalog→fabric open）已被 fabricCachedPath 修复覆盖 |
| geospatial/util/resource_uri.cpp | URI 词表（parse/规范化/凭据脱敏） | OK（非 open 站点；display() 脱敏已覆盖 vsicurl 内嵌凭据） |
| data/providers/virtual_raster_source_provider.cpp | 本地 VRT，无远程分支 | OK |

## 测试计划（待构建后执行）

1. 更新 test_io_fabric_object_store 的 pinned case（已完成文本修改：裸降级→typed 拒绝 + local passthrough 不变）。
2. 新增（并入本轨 WP06 套件）：fabricCachedPath 未装 cache + http → GeoError；装 cache({}) → /vsirangecache/ 包裹（既有断言已覆盖）；offline gate 开 + fabricOfflineRefusal 交互不变。
3. 重定向后目标受策略约束：range cache 的 per-hop redirect guard 已有实现（#1456/#1545 家族）——验证用 support/http_s3_server.h 本地回环 fixture（勿外网）。

## HANDOFF（T04）

- src/app/dialogs/stac_browser_dialog.cpp:244-265 仍 GDALOpenEx(裸 vsicurl)。建议：调用 geo::remote::safeRemoteRasterPath（后端 API 已稳定，返回 typed status+path）；失败时 UI 展示 status.code()+message。此文件归 T04，本轨不动。
