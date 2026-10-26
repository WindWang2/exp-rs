# R21-B TASKS — M0–M7 工作包

## M0 盘点（完成）
BASELINE/ISSUE_TRIAGE/OWNERSHIP/PR_COLLISION_MATRIX 已落盘；126 open issues 全量分诊。

## M1 B0 复验（构建完成后）
- R20-02 F1–F6 + R2-1 测试族两遍绿：model_runtime、model_catalog_v2、workspace_services、fault_injection
- #1608/#1609 复验（python worker 域回归族）
- 38 域族 sweep（O-B1）

## M2 修复波次（每项 RED→FIX→GREEN×2）
| ID | 缺陷 | 文件 | 测试落点 | RED 机制 |
|---|---|---|---|---|
| G1(A1) | canonical 空回退 → 同一缺失文件多拼法多 SourceKey | providers/ogr_vector_source_provider.cpp:38、gdal_raster_source_provider.cpp:200 | test_data_source_providers.cpp | symlink 目录下缺失文件 resolve() 返回带 symlink 的路径 ≠ 文件出现后的 canonical 路径 |
| G2(A4) | schema 探测失败当 fresh；INSERT OR REPLACE 无条件覆写版本戳 | data/artifact_store.cpp:227、data/workspace_catalog.cpp:136 | test_artifact_store.cpp | DELETE journal 模式 + 第二连接 EXCLUSIVE 锁住探测期 → 999 版本戳被覆写、open 返回 true（修复后应 fail-closed 拒绝或保持 999 不覆写） |
| G3(A5) | metrics 先落库、run 安装失败留孤儿 metric 行 | experiment/reproduction_bundle_import.cpp:378+ | test_experiment_consistency_r4.cpp | 空 targetExperimentId 导入含 metrics.json 的 bundle → run_metrics 表残留新 run_id 行 |
| G4(A6) | createDataset/createDraftVersion SELECT-then-INSERT，并发撞 PK 报 store_write_failed 而非 conflict | dataset/dataset_store.cpp:361+/609+ | test_dataset_core.cpp | 双 DatasetStore 实例（双连接）barrier 同轮插同一 id，败者错误码必须 dataset.conflict |
| G5(B5) | mask_threshold 无差别阈值化所有 head（含 uncertainty/embedding/detection/aux），违反 manifest confidence 语义注记 "must not threshold across semantics" | operators/runtime/tile_inference_engine.cpp:1819+ | test_model_runtime.cpp | 双头 manifest（segmentation+embedding）+ maskThreshold → embedding 带被二值化（修复后保持连续值） |
| G6(#1647-1) | SicnuPythonApi 288 行死代码；_SicnuHelper.project_info() 广告不存在的 sicnu.project_path() | python/qgis_python.cpp:148-207、sicnu_python_api.cpp | test_python_engine.cpp | 解释器内调用 sicnu.project_path() → AttributeError（修复后返回真实工程路径） |
| G7(R2-4) | feather-blend 变体 + 多输入 lane NoData 恢复无直接测试 | tests/test_model_runtime.cpp | 新增 2 用例（保护性，非 RED） |

## M3 性能/并发对抗
- dataset_store 并发压力（G4 延伸）、artifact pool 记账、推理吞吐小基准；ASan 可用则跑受影响族

## M4 功能深化
- G6 的 Python API 注册补完（project_path/save_project/active_layer 真实落地）
- 导入预检建议、模型能力/设备退化审计增强（沿用既有 public interface）

## M5 端到端 fixture
- 导入→数据集→实验重放→推理→原子发布→lineage（含中断/重启判定）

## M6 双轮独立审查 → 修复 → 再审
## M7 PR（不自动合并）
