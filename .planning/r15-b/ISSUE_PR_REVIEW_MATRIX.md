# R15-B Issue / PR / Review 矩阵

分类：FIXED_ON_MASTER / COVERED_BY_OPEN_PR / REPRODUCED_OPEN（本轮已修=→FIXED_HERE）/ NOT_REPRODUCIBLE / OUT_OF_SCOPE / LIVE_DEFERRED（复现或确认存在但本轮未修）。
证据形式：`代码定位`（master `46791bc6` 上的读码核实）或 `oracle`（test_r15_B_* 中可执行断言）。

## 1. 开放 PR
无（2026-10-16 20:00 UTC+8 `gh pr list --state open` = `[]`）。因此不存在 COVERED_BY_OPEN_PR 项；与 #1505/#1509/#1512 的去重以"已合并内容不重做"为准。

## 2. 线索 issue（本轨相关）

| Issue / 子项 | 分类 | 证据 |
|---|---|---|
| #1456 P1 stac_client IPv4-mapped IPv6 | FIXED_ON_MASTER | test_io_remote_validator Oracle-B2.1 通过；offline_gate 统一判定 |
| #1456 P1 stage_ledger 读路径窄字符 ifstream | FIXED_ON_MASTER | stage_ledger.cpp:220 已用 `pathFromUtf8` |
| #1456 P1 http_fetch 重定向绕过首跳 | FIXED_ON_MASTER | redirectGuard（#1485 用例、SEC-02 用例通过） |
| #1456 P2 query_planner truncated 覆盖 | FIXED_ON_MASTER | query_planner.cpp:373 `truncated = truncated \|\| selectorTruncated` |
| #1456 P2 mirror blockSize 未读 | FIXED_ON_MASTER | mirror.cpp:1231 |
| #1456 P2 requestPageDetached 生命周期 | FIXED_ON_MASTER（客户端私有 shared_ptr）；进程退出期回调为残留风险 → OUT_OF_SCOPE | stac_client.cpp:759-802 |
| #1457 P1 bounds-only 样本丢 sourceAssets | FIXED_ON_MASTER | sample_promotion.cpp:335-342 无条件附加 |
| #1457 P2 removeSample 非事务 | FIXED_ON_MASTER | dataset_store_samples.cpp:494 BEGIN… |
| #1457 P2 saveLabelSchema check-then-insert | FIXED_ON_MASTER | 单语句 INSERT + 冲突判定（samples.cpp:~89-98） |
| #1457 P2 data_manager relocate 忽略 write 返回 | FIXED_ON_MASTER | data_manager.cpp:773-782 |
| #1457 P2 stepRow 吞 sqlite 错误 | 部分 FIXED_ON_MASTER（`stepFailed()` 已有）；**ExperimentStore 4 个 Result 扫描 + 4 个 COUNT 仍把中途错误当成功 → REPRODUCED_OPEN → FIXED_HERE（B07）**；DatasetStore 中返回裸 `QVector` 的 10 个扫描 → LIVE_DEFERRED（需改 API） | oracle `B07:`（RED：60 行只返回 40 行且 success） |
| #1458 P1 portable 标签虚报 | FIXED_ON_MASTER | reproduction_bundle.cpp:236-242 拒绝导出 portable |
| #1458 dead `bool impossible` | FIXED_ON_MASTER | 已不存在 |
| #1448 experiment_store stepFailed 在循环内不可达 | FIXED_ON_MASTER（:2265、:2545、:1209） | 读码 |
| #1448 其余（workflow / teaching_admin / mcp_server） | OUT_OF_SCOPE（非本轨目录） | — |
| #1449 dataset_store / workspace_catalog / experiment_store close() 无锁 | FIXED_ON_MASTER（close 持锁 + close_v2） | dataset_store.cpp:294-308 等 |
| #1449 close() 与并发调用者的生命周期 | LIVE_DEFERRED（文档化限制）：等待在 `m_impl->mutex` 上的线程在 close 删除 Impl 后仍会 UAF；属于"调用方不得与 close 并发"的契约，修复需要改所有权模型 | 读码；未运行 TSan（见 TEST_RESULTS） |
| #1449 其他（plugin_registry 等） | OUT_OF_SCOPE | — |
| #1467 file-URI 派生调用方（core/temporal_cube、radiometric_state、qgsdatasourceresolver） | OUT_OF_SCOPE（src/core 只读/非本轨） | — |
| #1485 SSRF 字面量绕过（2130706433/127.1/0x7f…/[::]） | FIXED_ON_MASTER（#1505） | oracle `B16: #1481 SSRF…` 通过 |
| #1485 DNS rebinding 钉住解析结果 | LIVE（= #1505 T3 未闭环）；offline_gate 保守只读，未修 | 读码 |
| 其余开放 issue（#1499 #1495 #1494 #1491 #1490 #1489 #1488 #1486 #1468 #1466 #1465 #1464 #1463 #1462 #1460 #1459 #1455 #1454 #1447） | OUT_OF_SCOPE（python/ci/i18n/verification/teaching/agent/operators/processing/json） | 标题+目录归属 |

## 3. 合并 PR 的残留 review thread

### #1481（19 条，全部 unresolved）
| Thread | 路径 | 分类 | 证据 |
|---|---|---|---|
| T1 IPv6 压缩尾组越界写 | offline_gate.cpp | FIXED_ON_MASTER | parseSection 传剩余容量（:268）；oracle `[1:2:3:4:5:6:7::8:9]` 不崩溃 |
| T2 validator 每跳重定向守卫 | remote_source_validator.cpp | FIXED_ON_MASTER | SEC-02 / #1485 用例通过 |
| T3 数字回环拼写 | offline_gate.cpp | FIXED_ON_MASTER | oracle |
| T4 IPv6 zone id | offline_gate.cpp | FIXED_ON_MASTER | oracle `fe80::1%25eth0` |
| T5 maxItems 后续页 hasMore | catalog_service.cpp | FIXED_ON_MASTER | :786 `hasMore = index < all.size()` |
| T6 file://LOCALHOST 大小写 | resource_uri.cpp | 未核实（resource_uri 保守只读，未加 oracle）→ NOT VERIFIED |
| T7 解码后 NUL | resource_uri.cpp | 未核实 → NOT VERIFIED |
| T8 file URI 走 CheckedPath | resource_uri.cpp | 未核实 → NOT VERIFIED |
| T9 file URI 锚定 | resource_uri.cpp | 未核实 → NOT VERIFIED |
| T10 末页诊断累计 | catalog_service.cpp | FIXED_ON_MASTER | searchAll :977-978 在 return 前累计 |
| T11 组发布崩溃恢复 | atomic_fs.cpp | 部分 FIXED_ON_MASTER（#1504 引入持久 journal + recoverDatasetGroup）；**读者仍会打开混代组 → REPRODUCED_OPEN → FIXED_HERE（B13）** | oracle `B13:` |
| T12 repro bundle 加固未实现 | .planning | 部分 FIXED_ON_MASTER（checksum 路径穿越守卫）；**data/ 迁移跟随符号链接/未校验字节 → REPRODUCED_OPEN → FIXED_HERE（B12）** | oracle `B12:` |
| T13 IPv6 未指定地址 | offline_gate.cpp | FIXED_ON_MASTER | oracle `[::]` |
| T14 dataset_store prepare 失败传播 | dataset_store.cpp | FIXED_ON_MASTER（prepare 失败返回 `statement prepare failed` 诊断，:534） | 读码 |
| T15 远端 capped 尾部保留 | catalog_service.cpp | FIXED_ON_MASTER | test_io_fabric_catalog Thread #15 用例通过 |
| T16 百分号编码 host | offline_gate.cpp | FIXED_ON_MASTER | oracle `local%68ost` |
| T17 直接 COG 打开 offline 闸门 | cog_validator.cpp | FIXED_ON_MASTER | oracle `B16: #1481 T17` |
| T18 format profile 用解码路径 | format_profiles.cpp | FIXED_ON_MASTER | :386 `profileForPath( gdalPath )` |
| T19 file:/// 根规范形式 | resource_uri.cpp | 未核实 → NOT VERIFIED |

### #1504（0 条 thread）
- 引入的 resolver `blockPrivateNetworks=true` 使历史 oracle 失败且拒绝以异常逃逸 → REGRESSION_OF_EXISTING_FIX → FIXED_HERE（B04）。

### #1505（11 条，全部 unresolved；已合并）
| Thread | 分类 |
|---|---|
| T1 identity probe 传递私网守卫 | FIXED_ON_MASTER（resolver probeOptions `blockPrivateNetworks=true`）；但拒绝逃逸 → B04 |
| T3 DNS 结果钉住连接 | LIVE（offline_gate 只读，未修） |
| T4 多播 | FIXED_ON_MASTER（oracle `224.0.0.1`、`ff02::1`） |
| T6 重定向拒绝不得回落 /vsicurl | 未核实（range_cache 只读）→ NOT VERIFIED |
| T9 相对路径字面 `%` | 未核实（resource_uri 只读）→ NOT VERIFIED |
| T11 生产安装默认开启私网阻断 | 未核实（range_cache 只读）→ NOT VERIFIED |
| T2/T5/T7/T8/T10 | OUT_OF_SCOPE（python / gpu_plane / temporal_cube / science_context） |
