# R25-A ISSUE_TRIAGE — 候选 Issue 实时复核（对照 master 2610cb4741）

任务书候选：#1687、#1631、#1646、#1550、#1538。逐项以当前 master 代码复核：

| Issue | 状态 | 证据 |
|---|---|---|
| #1538 [P1] HistogramEqualize ±Inf UB | **FIXED_IN_MASTER_BUT_OPEN** | `src/processing/algorithms/image_enhancement_streaming.cpp:986` 已是 `if ( !std::isfinite( v ) \|\| v == nodata ) continue;`，注释直接引用 `#1538` |
| #1550 [P1] JM 可分性上溢静默报 0 | **FIXED_IN_MASTER_BUT_OPEN** | `src/analysis/classification/rs_jm_separability.cpp:153-176` 已改 log 域 `logDeterminant()` 计算，注释引用 `#1550`；NaN→-1.0 (n/a) |
| #1631-1 kFold std::shuffle 复现性 | **FIXED_IN_MASTER_BUT_OPEN** | `rs_cross_validation.cpp:60-70` 已手写 Fisher-Yates，注释说明与空间 CV 同纪律 |
| #1631-2 KMeans/ISODATA y 未校验 | **FIXED_IN_MASTER_BUT_OPEN** | `rs_classifier_kmeans.cpp:51-58` 已校验 `y.type()!=CV_32S \|\| y.cols!=1 \|\| y.rows!=X.rows` |
| #1631-3 FeatureScaler fromJson min/max 长度 | **FIXED_IN_MASTER_BUT_OPEN** | `rs_feature_scaler.cpp:163-168` 已四数组等长校验 fail-closed |
| #1646-1 preflight GDALAllRegister 绕过 | **FIXED_IN_MASTER_BUT_OPEN** | `algorithm_preflight.cpp:172,348` 已换 ensureGdalInit() 并注释 #692（框架目录，非 A 轨写域） |
| #1646-2 local_worker_pool 无锁读 | **FIXED_IN_MASTER_BUT_OPEN**（已文档化契约） | `local_worker_pool.cpp:336-340` 已加注释说明 effectively-immutable 契约（框架目录） |
| #1646-3 GdalDatasetWrapper move 丢 lastError | **FIXED_IN_MASTER_BUT_OPEN** | `gdal_dataset_wrapper.cpp:67-88` 移动构造/赋值已转移 `m_lastError` |
| #1646-4 json_params_converter.h 重复前向声明 | **PARTIAL/低价值**（框架目录，非 A 轨写域） | 头文件仍有两处 inline 前向声明（:19-20 与 :60-61 区域），属复制残留；登记 CROSS_TRACK，不在本轨擅改 |
| #1646-5 帮助目录 14 死键 | **FIXED_IN_MASTER_BUT_OPEN** | `algorithm_help_catalog.cpp:25-90` 键已改为真实注册名（gdaldem/gdalinfo/centroids/native_clip/cliprasterbyextent/hillshade/native_intersection/native_union 等） |
| #1646-6 nodata_utils 未被消费 | **FIXED_IN_MASTER_BUT_OPEN** | `resampler.cpp:23`、`spectral_anomaly.cpp:29`、`spectral_local_rx.cpp:30` 及 10+ operators/rs 均已走 `sicnu::rs::isNoDataValue/bandNoDataSentinel` |
| #1687-1 math_utils 硬编码 -999 | **FIXED_IN_MASTER_BUT_OPEN** | `math_utils.cpp:178+` `bothNormalizable` 已只拒非有限值，注释引用 #1687；legacy 哨兵迁移至具名 `legacyNegativeSentinelFree`（dNBR 专用） |
| #1687-2 computeStatsFromAccumulators 语义文档 | **FIXED_IN_MASTER_BUT_OPEN** | `math_utils.cpp:142-176` 已有 population stddev + #1684 精度注释 |

## 结论
任务书候选全部已在 #1718/#1722 波次修复。**≥4 新缺陷必须来自本轨 W0~W5 深审**；若实际不足不凑数，以可验证的算子功能深化补足（任务书 §7.2 明文允许）。

## 后续深审焦点（W0 展开）
- `src/operators/rs/` 259 个条目中的科学计算核（W1-W4 分域）
- `src/analysis/{classification,atmospheric,georeferencing,hyperspectral,segmentation}/`
- `src/processing/{algorithms,gdal}/`
- 历史 DECISIONS 中被标「未确证候选」：R24-A 的 SAR 元数据回退、太阳纬度默认、累积精度
