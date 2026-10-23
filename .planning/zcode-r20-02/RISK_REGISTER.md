# R20-02 RISK_REGISTER — 已知但本轮不修（真实、非虚构，供后续轮次）

| ID | 候选 | 位置 | 不修理由 |
|---|---|---|---|
| A1 | canonicalFilePath 为空回退 absoluteFilePath → 同一物理文件双 AssetId（symlink/大小写变体）| src/data/providers/ogr_vector_source_provider.cpp:38、gdal_raster_source_provider.cpp:200 | 修复牵 Missing→Available 状态迁移时的 SourceKey 重算与 reindexSourceKey 复用，改动面大；需独立设计轮 |
| A4 | schema 探测 prepare 失败被当 fresh DB，版本戳可被 INSERT OR REPLACE 覆写 | src/data/artifact_store.cpp:268、workspace_catalog.cpp:202 | WAL 下读路径几乎不 BUSY；确定性 RED 触发（第二连接持锁/媒介 IOERR）在本测试环境不稳定，拒绝无 oracle 的修复 |
| A5 | bundle 导入 metrics 先落库、run 安装失败留孤儿 metric 行 | src/experiment/reproduction_bundle_import.cpp:411/436 | 需要 fault 点 arm 的确定性注入；顺序倒置修复简单但无既有 fault-arm 测试模式可循，风险>收益本轮 |
| A6 | createDataset/createDraftVersion 裸 SELECT-then-INSERT，并发冲突误报 store_write_failed 而非 conflict | src/dataset/dataset_store.cpp:361/371、609/619/685 | PK 保护数据无损，纯诊断码问题；与 #1390/#1457 同型修复已有模板，留待下轮 |
| B5 | mask_threshold 无差别阈值化所有 head（含 embedding/auxiliary）| src/operators/runtime/tile_inference_engine.cpp:1802-1808 | 多头+mask_threshold 组合在随库 manifest 无实例；需先定义"阈值化只作用于类别头"的契约讨论 |
| #1597 残余 | pipeline/workflow/data/catalog-export CLI 残留 flag 黑洞 | src/cli/cli_commands.cpp:549+/643+/2128+/2702+ | src/cli 非本轨权域 → CROSS_TRACK_FINDINGS |
| #1595 残余 | data catalog search --max-items ok 未检 | src/cli/cli_commands.cpp:2400 | 同上 |
| #1647-1 | SicnuPythonApi 从未注册进解释器 | src/python/sicnu_python_api.cpp | src/python 顶层非本轨权域 → CROSS_TRACK_FINDINGS |
| #1537 | STAC searchNext 续翻旧查询分页 | src/app | 04 轨域 → CROSS_TRACK_FINDINGS |

## 平台限制声明（NOT_EXECUTED）
- 无 GPU/无 CUDA：GPU lane 测试未跑。
- 无 macOS/Windows lane：#1456（Windows 注释红）等未执行。
- TSan 未运行（宿主无 TSan 配方，R19-01 同声明）。

| R2-4 | F2 feather-blend 变体与多输入 lane NoData 恢复的直接测试用例（当前为同构代码 + 纵深防御语义，R2 核实无缺陷） | tests/test_model_runtime*.cpp | 跟进轮补 2 个用例 |
