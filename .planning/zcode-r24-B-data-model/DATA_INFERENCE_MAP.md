# R24-B DATA_INFERENCE_MAP — 真实数据→运行时→产物→实验记录链（O-B0）

基线：master `5fe642ec6` 实读（file:line 均为本轨侦察代理在该 HEAD 的实读）。

## 1. 导入与产品识别（src/geospatial）

| 阶段 | 代码 | 说明 |
|---|---|---|
| 格式认证 | `formats/format_profiles.*` | Certified vs Accessible：只有带往返保真测试的格式才可承诺 |
| 产品识别 | `products/product_registry.*` + `product_adapters.*` + `cn_product_adapters.*` + `sensor_profile.*` | Landsat MTL / S1-S2 SAFE / MODIS / GF/ZY/HJ 家族；完备性判定（ADR 0137）；传感器真值 JSON |
| 规范元数据 | `metadata/canonical_metadata.{h,cpp}` | 全部适配器映射进出的唯一词表；一次 GDAL open，无全像元扫描。**R24-B**：`WAVELENGTH_UNITS` 归一化（µm→nm，未知单位=缺失不猜测） |
| URI/路径 | `util/resource_uri.cpp` `resolveAgainst` | 相对引用解析；仅拒 NUL/`..`（#1579 修复在位；R24-B 补正例回归） |
| COG 出口 | `cog/cog_presets.*` + `cog_validator.*` | 唯一批准的 COG 创建预设 + 结构校验 |
| STAC | `stac/stac_client.*` + `stac_mapper.*` | 只读 Item Search/Collections + Item↔CanonicalMetadata 双向 |

## 2. 远程访问与缓存（src/geospatial/remote）

- `/vsirangecache/` VSI handler（`range_cache.cpp`）：有界 LRU + 校验和磁盘层（`range_cache_disk.cpp`，几何键含 blockSize，原子组发布：唯一 tmp→fsync→rename）。
- 受控传输（`offline_gate.cpp` authority 按 RFC 3986 先裁 `/?#` 再 `rfind('@')`；DNS pin fail-closed；probe/revalidate/Stat 同受 SSRF 策略约束——`validatorOptions()` 传播 blockPrivateNetworks）。
- 身份校验 RFC 7232（ETag 强/弱、Last-Modified）驱动缓存陈旧性。

## 3. 数据资产与数据集（src/data + src/dataset）

- 运行时资产权威 `data/data_manager.*`（信号 assetAdded/Changed/Removed）；治理库 `data/governance/governance_store.*`（SQLite WAL、schema_version、批量单事务；`workspace_service.cpp` mirrorAllAssets 失败通道 + R24-B ghost-remove 计数）。
- DatasetStore（`dataset/dataset_store.cpp` + `_samples/_splits/_facets`，SQLite WAL，BEGIN IMMEDIATE + checked commit + 自动回滚）：
  - 版本生命周期 createDraft→(mutate)→stage→commit；**R24-B：六个写路径的可变性门全部移入事务内**（addSamples/removeSample/addAnnotation/addSourceAssetsToDraft/stageVersion/commitVersionImpl，#811 范本）。
  - **R24-B**：schema 门读取三态化（读失败≠新库，拒绝打开而非倒写版本戳）。
  - 指纹 `dataset_fingerprint.*`（RFC8785 规范化 SHA-256）；split 引擎 `split.*`（PCG32/SplitMix64，byte-identical 重放）；泄漏审计 `leakage_audit.cpp`（网格半空间枚举每对一次；**R24-B：bucketOf 转换护栏 + unbucketed_count 量化**）；fold 审计 `fold_audit.cpp`（基数从 assignments 推导）。
  - sample_promotion：Draft→Committed 样本晋升 + provenance 闭环（失败=dataset.promotion_partial）。

## 4. 模型清单与 preflight（src/operators/runtime）

- `model_catalog.*`（framework 侧，只读引用）→ ModelInfo；`eo_preflight.cpp`：校准态/波长窗/CRS 族/网格。**R24-B**：波长窗在 band_roles 缺席时命名告警（不再静默不执行）；角色超出显式选带时命名（不再检查未喂的 band）。
- `device_planner.*`：CPU/CUDA/auto 选择、VRAM 台账（per-acquisition ticket）；`model_runtime.cpp`：注册表/会话池/LRU/回退链。
- `model_execution_service.runModelInference`：唯一入口（无第二路径）。

## 5. Tile 推理与检测（src/operators/runtime + src/runtime）

- `tile_inference_engine.cpp`（~4300 行）：分块+halo、羽化混合（per-slot 缓冲，#1709）、批次/OOM 阶梯、`effectiveBatchSize`（fedW=tile+2·halo 与 to_input 取 max）、原子发布（stage→ProductPublishGuard park/swap/sidecar）。
- `detection_tile_engine.cpp` + `detection_postprocess.cpp` + `detection_fusion.cpp`：中心-在-核内 seam 规则、全图 NMS（全序 tie-break）、WBF。**R24-B：声明式 NoData 哨兵→NaN→0（与 raster/scene lane 对齐）**。
- `src/runtime/chunk|exec|gpu|observability`：chunk DAG（单消费者守卫）、执行 governor（RSS 节流 30s deadline）、会话池（析构持锁）、遥测环（游标覆写）。
- **R24-B 发布面**：`model_publish.*` — 跨进程 QLockFile 占用栅栏 + 唯一 stage 名 `<final>.<pid>x<n>.tmp~` + 年龄门控 sweep + 栅栏拒绝时 stage 清理。

## 6. 实验、谱系与复现（src/experiment）

- 记录链：TaskCenter/Workflow → `run_bridge`（ExecutionEvent 状态机）→ `run_recorder` → ExperimentStore（SQLite WAL；execution_fingerprint 索引列=身份孪生查找）。
- 身份：`runExecutionFingerprint`（RFC8785 canonicalize SHA-256 over algorithm/dataset/split/model/seed/revision）；identity pins 不可变。
- 偏差判定只有两处：`replay_deviation.cpp`（identity-only 门）与 `repeat_execution.cpp`。
- 复现包：`reproduction_bundle.cpp` 导出（checksums.txt 全覆盖 + 双向校验；**R24-B：run_edges 方向保持 from/to + 兼容 other_* 键**）→ `reproduction_bundle_import.cpp`（完整性门→身份门→metrics 先装→run 后装→**R24-B：lineage 重挂（run 端点改写为安装 id、敌意项命名跳过、数据集侧边需可选 DatasetStore）**）。
- 统一谱系：`lineage.cpp` BFS（visited 环切、depth/node 预算）合并两库边表。

## 7. 关键对象生命周期/事务标注

- DatasetStore/ExperimentStore：每写 API 单事务（R24-B 后含门检查）；WAL + busy_timeout 5000 支持跨连接。
- 发布：stage（唯一名）→ ProductPublishGuard（跨进程栅栏）→ park(.prev~) → rename swap → sidecar staged+fsync+rename → disarm；崩溃窗口 A/B/C 由下一次发布收养。
- 会话：ModelRuntimeRegistry LRU/VRAM 台账，票随会话包装；evictStale 与 acquire 全程 m_mutex 串行。
- 缓存：RangeDiskBlockStore 键=(URL/etag + blockSize)；进程策略强制 blockPrivateNetworks。

## 8. 本轨 E2E 链（O-B1 实跑入口）

- E2E-A 导入+身份+缓存：test_r24_b_uri_percent（解析）+ test_io_range_cache 邻域回归（SSRF/几何键）。
- E2E-B samples→split/audit→推理→label：test_r24_b_dataset_txn_gate（双连接竞态合同）+ test_r24_b_metamorphic（tile 不变性）+ test_r24_b_detection_nodata（NoData 链）。
- E2E-C 取消/原子发布/重放/重挂：test_r24_b_publish_fence（栅栏+stage 清理+回滚语义）+ test_r24_b_lineage_reattach（导出→导入→重挂→遍历 de-islanding）+ test_r24_b_leakage_bucket（审计护栏）。
