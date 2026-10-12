# PR/Issue/Review 对照矩阵 — r9-science-data-integrity

来源：实时 gh 查询（2026-10-12）+ subagent 只读核对 + 主 Agent 源码复核。
判定：ALREADY_FIXED_IN_MASTER（不重做）/ STILL_OPEN（R9 采纳或记录）/ NOT_A_SCOPE（归属 B/C）。

## A. 已合并 PR 的未解决 inline review threads（isResolved=false，isOutdated=false）

| PR | 文件:行 | finding | 复核结论 | R9 动作 |
|---|---|---|---|---|
| #1469 | src/core/radiometric_state.cpp:55 | 非盘符冒号一律判 provider URI → POSIX 冒号路径跳过辐射元数据 | 主 Agent 复核确认 | **W1 采纳** |
| #1472 | src/core/radiometric_state.h:59 | ModelEoDomainContract::validate kStates 缺 sigma0/beta0（SAR manifest 被拒） | 确认（kStates 在 model_catalog.cpp:1379） | **W2 采纳** |
| #1472 | src/core/qgsdatasourceresolver.cpp:93 | file://localhost 经 toLocalFile 得 SMB 型路径，localhost 分支不可达 | 确认 | **W3 采纳** |
| #1472 | tests/test_datasource_resolver.cpp:189 | percent-decode 断言未平台感知（POSIX 得 `/C:/…`） | 确认（伴随 W3） | **W3-companion 采纳** |
| #1472 | src/processing/algorithms/temporal/temporal_workspace.cpp:538 | 仅 scenes 参数规范化 file://，通用候选循环漏 | 确认 | **W4 采纳** |
| #1472 | src/processing/gdal/gdal_dataset_wrapper.cpp:93 | params::fileExists 原始串喂 QFile::exists → file:// 被 FileNotFound 拒 | 确认（实修点在 operators/framework/rs_json_params.cpp:18） | **W7 采纳（跨界最小，PR 标注）** |
| #1473 | src/data/data_manager.cpp:786 | durable_sidecar VerifyFailed（已发布）被布尔吞掉 assetChanged | 确认（WriteStatus 语义：VerifyFailed/LastGoodFailed=目标已换字节） | **W5 采纳** |
| #1473 | src/dataset/dataset_store_samples.cpp:503 | prepare 失败分支在 rollback 后读 stmt.error() → "not an error"（stmt+facetStmt 两处） | 确认（step 分支已修，prepare 两处漏） | **W6 采纳** |
| #1469 | src/agent/mcp_server.cpp:2663 | recording enable 失败时 workflow 已派发 | 属 B（src/agent） | 不做，记录相邻问题 |
| #1469 | src/operators/runtime/model_runtime.cpp:984 | GPU 唯一准入并发合并 | 属 B | 不做 |
| #1469 | src/sdk/exprs/plugin_registry.cpp:273 | 快照无界增长 | 属 B | 不做 |
| #1469 | src/sdk/exprs/ipc_channel.cpp:428 | 状态锁内回调 | 属 B | 不做 |
| #1469 | src/sdk/exprs/external_process.cpp:525 | 前斜杠 Windows 根路径 | 属 B | 不做 |
| #1472 | run_exe.cmd:10 | 退出码被 echo 抹掉 | 构建脚本卫生（旧 r8 lane） | 不做（r9 脚本自含正确传播） |

## B. 开放 Issue 核对（摘要，全文见 subagent 矩阵归档于本文件 §D）

| Issue | 判定汇总 | R9 动作 |
|---|---|---|
| #1461 analysis 簇（8 子项） | 全部 ALREADY_FIXED_IN_MASTER（multires NoData、classId 宽度、transform fail-closed、segment_map 原子写、scaler.fit、MinDistance 维度、跨层 include、错误通道） | 不重做 |
| #1456 geospatial 簇（6 子项） | 5 已修（IPv4-mapped、ledger 方言、redirect egress、mirror blockSize、detached lifetime）；**query_planner 截断信号覆盖 STILL_OPEN** | **N1 采纳** |
| #1457 data/dataset 簇（6 子项） | 5 已修（sourceAssets、removeSample 事务、saveLabelSchema 幂等、relocate VRT sidecar、线程亲和）；stepFailed **部分**：facets/splits/workspace_catalog 残留循环 | **N3 采纳**（W6 同文件另修） |
| #1458 experiment 簇（2 子项） | 全部已修（portable typed 拒绝 + 死 bool 移除） | 不重做 |
| #1467 core-fork 簇（6 子项） | 4 已修（scene-read NaN 可观测、GDALOpenEx provider 跳过、plugin QWidget、tStart+tCount 溢出、file:// 处理）；冒号误判归 W1；分层债（spatial_split 双实现）记录 | W1 采纳；分层债记录不做 |
| #1450 authority 簇（6 子项） | vocabulary 双轨已修；canonical_json locale 已修；checkpoint bounded reader=NOT_A_SCOPE(B,已修)；**suitability 摘要绕过 canonicalizer STILL_OPEN**；SHA-256 fork（≥7 份）记录为债 | **N2 采纳**；SHA-256 债记录 |
| #1448 stores 簇（6 子项） | 范围内 3 项全修（experiment_store 扫描/分页）；其余 NOT_A_SCOPE（B workflow / C teaching） | 不做 |
| #1449 concurrency 簇（7 子项） | 范围内 3 项全修（三处 close 持锁）；asset_state_provider setResolver/resolve 无锁读写潜在（science_context，C 协调归属） | 记录相邻问题（供 C/后续） |
| #1447 json 簇（9 子项） | 范围内 3 项全修（fault_transforms/capability_router/worker_main）；其余 NOT_A_SCOPE 且也已修 | 不做 |
| #1464 verify/grader/faultlab（2 子项） | 全部已修（parseSequencingStamp、可选参数消费） | 不做 |

## C. 冲突矩阵（path + symbol + Issue + PR）

| 路径 | R9 触碰 | 相邻方向风险 |
|---|---|---|
| src/operators/framework/model_catalog.cpp（kStates）/rs_json_params.cpp（fileExists） | B 侧文件，各 1 处最小契约变更 | PR 明示供 B 串行集成；不改 B 契约语义 |
| tests/CMakeLists.txt | 若新增测试文件需注册 | 仅追加本方向测试条目（历史各 PR 同模式） |
| src/processing/gdal/gdal_dataset_wrapper.cpp | 不改（W7 修在 rs_json_params 侧） | 无 |
| resources/icons（symlink→junction） | 仅本地构建需要，不入提交 | 无 |

## D. subagent ① 完整矩阵归档

（保留原文于 git 历史：agentId 59904c41，2026-10-12；关键 file:line 证据已在 §A/§B 引用，复核以主 Agent 读码为准。）
