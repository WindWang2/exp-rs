# R21-B BASELINE — 地理空间数据、模型推理、计算运行时与实验可复现

## 执行时点事实（2026-10-24 02:4x–03:0x UTC 现场采集）
- **origin/master = `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`**（与任务锚点一致；git fetch 经 socks5 代理间歇可用，最终按需取齐 blob）
- 本地 master（主工作树）= `28e1891390`（behind 59，clean，未触碰）
- **Open PR = 0**；最近合并：R20 五轨 #1668/#1669/#1670/#1671/#1672（2026-10-23）
- **Open Issues = 126**（已全量落盘 `open_issues_raw.tsv`）
- 本轨 worktree：`/home/kevin/project/exp-rs-r21-B-20261023T185101Z`，branch `agent/r21-b-data-model-runtime-20261023T185101Z`，base=911e0b17
- 旧 `agent/r20-*` 分支均已合并，无 ahead（不作为 worktree 基底）

## 环境
- 主机 cachyos x86_64，40 核/62GB；跨轨锁 `/home/kevin/project/exp-rs-build.lock`（沿用 R19/R20 约定）；本轨 -j2
- GDAL/PROJ/GEOS/GSL/NetCDF 来自 `paleo-singlefactor-closure/vendor/prefix/usr`（r19-01 同配方）；Qt6/OpenCV 系统
- CMake 4.4.x / Release / ENABLE_TESTS=ON / ENABLE_LOCAL_BUILD_SHORTCUTS=ON；build dir `build-r21-B`
- gh CLI 经 api.github.com 可用；git https 经代理间歇失败（重试可恢复）

## R20-02（PR #1669）成果在 911e0b17 的源码级复验
| 项 | 状态 | 证据（现 master） |
|---|---|---|
| F1 边缘 tile 混批 | 在位 | tile_inference_engine.cpp:1287 batchCores 平行向量；几何断点检查后统一 push |
| F1-R2 probe flush 前置 | 在位 | :2312 `if (!deferredNoData.empty() && !writer && !batchMats.empty()) flushPendingBatch(...)` |
| F2 uncertainty NoData NaN | 在位 | :3846 uncRow NaN 写入 |
| F3 interpolation/resize 词表 | 在位 | model_catalog.cpp:1071-1079 全局门 + :1431 per-feed 非空即拒 |
| F4 uncertainty 无 2 通道头 typed refusal | 在位 | tile_inference_engine.cpp:3692 注释门 |
| F5 快照 DB 拷贝失败检查 | 在位（文件已迁 `src/data/governance/workspace_lifecycle.cpp:109`） | "DB copy failed" |
| F6 对象池自愈+fsync+dirsync | 在位 | artifact_object_pool.cpp:127-183 |
- 运行时复验（测试两遍绿）在 M1 构建完成后执行。

## RISK_REGISTER（R20-02）遗留项在 911e0b17 的再核实
| ID | 状态 | 现证据 |
|---|---|---|
| A1 canonical 空回退双 AssetId | **仍在位** | ogr_vector_source_provider.cpp:38、gdal_raster_source_provider.cpp:200 |
| A4 探测失败当 fresh DB、版本戳覆写 | **仍在位** | artifact_store.cpp:227 `INSERT OR REPLACE ... schema_version`；workspace_catalog.cpp:136 |
| A5 bundle 导入孤儿 metric 行 | **仍在位** | reproduction_bundle_import.cpp:378+（metrics 先落库；upsertRun 失败无补偿） |
| A6 createDataset TOCTOU 误码 | **仍在位** | dataset_store.cpp:361+ 裸 SELECT-then-INSERT |
| B5 mask_threshold 阈值化所有 head | **仍在位** | tile_inference_engine.cpp:1819+（Probability 模式无差别阈值化） |
| R2-4 blend/多输入恢复直测 | **缺口仍在** | 无对应直接用例 |
| #1647-1 SicnuPythonApi 未注册 | **仍在位** | qgis_python.cpp:148-207 纯 Python shim；C++ API 0 注册点；project_info() 广告不存在的 sicnu.project_path() |

## 新归入本轨权域且已核实已修（待运行时复验）
- #1608（execute_algorithm 返回值 json 安全化 + 预编码 typed -32000）：worker_daemon.py:619-651
- #1609（unload_plugin 清理 callbacks/algo_executors）：worker_daemon.py:206-209,469-483

## 候选 backlog（按优先级）
P0/P1 波次（M2）：A1、A4、A5、A6、B5、#1647-1、R2-4 测试缺口
M1：R20 F1–F6 运行时两遍绿（O-B1）
M3/M4/M5：压测 mock、导入预检建议、端到端 fixture（导入→split→重放→推理→发布→lineage）
