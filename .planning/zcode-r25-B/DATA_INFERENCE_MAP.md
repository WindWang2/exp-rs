# R25-B DATA_INFERENCE_MAP — 资产→模型→实验→复现 资产流实码链路

基线 `2610cb4741`（2026-10-27）。每跳附 file:line 与身份/事务/失败语义。

## 1. 导入与资产身份（src/data + src/geospatial）

```
本地路径/STAC/remote COG
  → ProductAdapterRegistry::adapterFor/describe          src/geospatial/products/product_registry.h:107-110
    （首匹配即赢；completeness=Invalid/UnsupportedVersion → typed GeoError）
  → ProductMetadata（sensor/bands/CRS/time/cloud）        src/geospatial/products/product_adapters.h:50-90
  → [R25-B F1] ImportPreflight::inspect                   src/data/import_preflight.h:160
    （只读解释报告：identity/dedup 预测/歧义候选/typed refusal，零副作用）
  → DataManager::registerSource(RegisterRequest)          src/data/data_manager.cpp:452-545
    （SourceKey=providerKey+canonicalSource+subdataset+dataOptions 去重；
     结构变化→revision++/assetChanged；fingerprint 相同→静默复用）
  → AssetSnapshot（RasterStructure/bands/roles）          src/data/data_asset.h:120-147
远程源：
  → remote asset 注册（延迟网络打开）                      src/data/governance/import_center.h:60-66
  → identity token ri1/li1（fail-closed）                 src/geospatial/identity/asset_identity.h:87-98
  → probe per-hop SSRF guard（r23-B W4）                  src/geospatial/remote/probe_remote.* + http_fetch.*
```

失败路径：`import.path_missing` / `import.unreadable` / `import.product_*`（F1 报告）↔
`registerSource` 自身 Diagnostic 集合（source.invalid 等）。

## 2. 数据集与划分（src/dataset）

```
DatasetStore（SQLite）                                     src/dataset/dataset_store.h
  addSamples/removeSample/addAnnotation/addSourceAssetsToDraft
  /stageVersion/commitVersion —— 六写路径全部 BEGIN IMMEDIATE 事务门（r24-B WP-B3）
  draft→staged→committed 状态机；样本冻结进已提交版本=typed 拒绝
  leakage_audit（空间桶 + qint64 UB 护栏 r24-B WP-B4）     src/dataset/leakage_audit.cpp
  确定性随机（deterministic_random）+ fold_audit
```

## 3. 模型 catalog 与推理（src/operators）

```
ModelCatalog（models/*/model.json，manifest 4.0 身份）      src/operators/framework/model_catalog.h
  resolveModelReference（直连路径|catalog；readiness!=Ready→typed） src/operators/runtime/model_execution_service.cpp:24-68
  [R25-B F2] planModelInference（只读干跑：同一 gate 序列+网格计划）  src/operators/runtime/inference_plan.h
  runModelInference —— 唯一执行 seam                        model_execution_service.cpp:132-505
    gates: rejectUnwiredContracts / preflightFeatureCube / runtime readiness
           / lane 检查 / device / band 范围 / head 契约（r23-B）/ EO preflight / blend
    → ModelRuntimeRegistry::acquireWithFallback（session 池；OOM ladder）
    → TileInferenceEngine::run / runMultiInput              tile_inference_engine.cpp:1312+
       （tile 网格 tileSize=min(effectiveTileSize,max(W,H))；尾批 flush r24-B WP-B2；
        NoData 跳过；classPixelCounts 流式统计；GridProvenance）
    → DetectionTileEngine（向量输出+NMS dedup）             detection_tile_engine.cpp
    → 原子发布 + provenance sidecar（exp-rs-prov/1）        model_publish.cpp / publish fence（r24-B WP-B7）
  [R25-B F2] summarizeInferenceQuality（覆盖/网格完整性/类分布/抑制比）
```

## 4. 实验记录与复现（src/experiment）

```
ExperimentRunRecorder::startRun（身份 pin 采集）             src/experiment/run_recorder.h
  markSucceeded（run+metric+artifact 单事务 r23-B W3）       experiment_store.cpp:upsertRunWithMetric
  lineage 方向保持导出/导入重挂（r24-B WP-B7）              reproduction_bundle*.cpp
  capsule（capsule_builder/diff/readiness/portability）     src/experiment/capsule/
  [R25-B F3] ReplayDeviationAnalyzer v2                    src/experiment/replay_deviation.cpp:93+
    （verdict 语义不变；deviation_details[]：domain 归属+remediation）
```

## 5. 最小实测（每条路径）

| 路径 | 实测 | 位置 |
|---|---|---|
| 识别→登记→去重预测 | F1 测试：真实 DataManager 注册后 preflight 预测复用且零副作用 | test_r25_b_import_preflight.cpp |
| 计划→执行一致性 | F2 metamorphic：plan.tilesPlanned==engine stats（含尾块几何） | test_r25_b_inference_plan.cpp |
| 推理→原子发布→质量 | F2：真实 engine.run 后 summary 覆盖率 1.0、gridComplete | 同上 |
| 实验→复现→差异归因 | F3：deviated run 的 model/dataset/config/environment 维度归属 | test_r25_b_replay_deviation_v2.cpp |
| 事务门（邻域回归钉） | r24_b_dataset_txn_gate / r23_b_run_metric_transaction 双连接 racer | 既有测试，本轨复跑 |
| 错误路径 | F1 六类 typed refusal；F2 六个 gate refusal；F3 unknown-run typed | 上述三测试 |
