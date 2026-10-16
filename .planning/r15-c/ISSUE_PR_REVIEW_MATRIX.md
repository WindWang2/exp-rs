# R15-C Issue / PR Review Matrix — 逐条按最新 master 代码判定

判定词汇：`FIXED_ON_MASTER` / `COVERED_BY_OPEN_PR` / `REPRODUCED_OPEN` / `NOT_REPRODUCIBLE` / `OUT_OF_SCOPE`。
规则：GitHub 状态 OPEN ≠ 未修；每条以 base SHA `46791bc6` 的代码/测试为准。

## Open PR 去重（执行时实况）

| PR | 状态 | 与本轨关系 | 动作 |
|---|---|---|---|
| #1514 r15-A | open (7fc4382f) | 文件零重叠 | 只读关注 |
| #1513 r15-B | open draft (bddf36ab) | 仅 `tests/CMakeLists.txt` 尾部追加 | 错开插入点；不 cherry-pick |
| #1505 r13-j | **merged** 2026-10-16 2:58 | gpu_plane/python-pool/SSRF | 只回归验证，不重写 |
| #1509 r13-k | **merged**（本 BASE 已含） | jsoncpp typed safety | 不重做 |
| #1512 r14-D | **merged** | Qt/MapSpec | 无重叠 |

## 本轨相关 Issue 逐条

| # | 主题（摘要） | 分类 | 证据 / 后续动作 |
|---|---|---|---|
| #1455 | operators runtime/io cluster：corpse-session VRAM ledger；fabric io CancelToken；OTB watchdog 1h | 部分 FIXED_ON_MASTER；部分 OUT_OF_SCOPE | corpse-session ledger：R14-B 验证 #1501 已修（holder tickets + test_model_failure_matrix 回归）→ 本轨回归复跑。`io_fabric_operators.cpp:104-108` 的 `contextCancelToken()` 显示 CancelToken 已从 context 派生（疑似 #1501/#1504 修复，待代码走读确认）。OTB watchdog 在 `src/operators/otb/**`（A/B 轨可写范围外）→ 只读记录，不修 |
| #1460 | cli/sdk cluster：external_process argv/CWD/PATHEXT；模型参数只读协调 | 模型参数部分 → C14 审计中；external_process 部分 OUT_OF_SCOPE（sdk 属他轨） | C14 subagent 对照 rs:infer/segment/detect/embedding 的 CLI/SDK/Agent/UI 参数与默认值；发现只记录给 owner |
| #1486 | python worker pool 数据竞争（handleWorkerLoss/重启回调无锁改写） | 待确认 FIXED_ON_MASTER（#1505 merged 触及该文件） | configure+编译通过 + 代码走读确认；不在本轨写 `src/python/isolated/**` |
| #1488 | canonical_json asInt64 抛 Json::LogicError | FIXED_ON_MASTER（#1509，BASE 已含） | 不重做 |
| #1485 | SSRF 内网判定字符串可绕过 | FIXED_ON_MASTER（#1505） | 不重做（geospatial 亦非本轨） |
| #1447 | jsoncpp untyped access 9 sites | FIXED_ON_MASTER（#1509） | 不重做 |
| #1499 | master 编译断裂（python_worker_process_pool.cpp 多余 '}'） | 待编译确认（R14-B 在 f72f0c11 验过 exit 0） | 本轨 configure+build 即验证 |
| #1495/#1494/#1491/#1490 | CI/clang-tidy/孤儿测试注册 | OUT_OF_SCOPE（禁改 CI；build/test 归 r13-i） | 只读记录；#1494 的孤儿测试若仍在，报告给 build 轨 |
| #1489 | i18n lupdate 只扫 src/app | OUT_OF_SCOPE（D 轨） | 不碰 |
| #1458 | experiment reproduction bundles | OUT_OF_SCOPE（#1513 范围） | 不碰 |
| #1456 | geospatial egress/cache cluster | OUT_OF_SCOPE + #1505 已 merged | 不碰 |
| #1459 | workflow/runtime cluster（scratch digest 等） | 主要 FIXED_ON_MASTER（#1501 streamFnv1a，R14-B 复核） | 若 C12 审计发现 chunk 侧残留，按本轨所有权处理 |
| #1454 | processing task-center cluster | OUT_OF_SCOPE（A 轨） | 不碰 |
| #1449/#1448 | concurrency/stores clusters | 大部分 OUT_OF_SCOPE | 只读；若命中 `src/runtime/**` 子项则评估 |
| #1462-#1468 | python/plugins/help/teaching/core-fork clusters | OUT_OF_SCOPE（#1505/#1501/#1511/#1510/#1507 已大面积处理） | 不碰；#1468 的 fixture server 超时属 build 轨 |

## R14-B 遗留项（本轨正当工作）

| 项 | 归属 | 工作包 |
|---|---|---|
| model_catalog 注册期 square-head 禁入缺口（A3 只在推理期拒绝） | framework owner = 本轨 | C02 |
| 性能实测缺失（装载/tile/解码/发布/p50/p95/RSS） | 本轨 | C15 |
| HTTP helper-thread exchange follow-up（R14-B LEDGER decision 2，记录为后续） | 本轨 | C10 评估 |
| maxDetections 注释漂移（model_catalog.h） | 本轨 | C08 顺手 |

## 待本轨编译/测试后更新

- #1499 / #1486 的最终判定（configure + build + 代码走读）。
- 各审计 subagent 回传后的新发现分类。
