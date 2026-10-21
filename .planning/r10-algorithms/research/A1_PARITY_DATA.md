# A1 算子注册真值 × manifest/sidecar 奇偶校验数据（R10-A / S1a）

- worktree: `exp-rs-r10-algorithms`（分支 `hardening/r10-algorithm-processing-quality`，基线 master 8a0864276）
- 方法：纯静态盘点（未编译、未改任何源码）。注册真值取自各家族 `init*Operators()` 的 **guaranteed add() 路径**（#707：REGISTER 宏静态初始化器可被链接器 dead-strip，显式表是唯一保证路径）；两者已逐一比对。
- 快照日期：2026-10-13。

## 1. 注册真值总表

**家族 5 个，注册算子 189 个**：rs 157（其中 17 个 `SICNU_HAS_OPENCV` 门控）、io 17、opencv 6、gdal 5、otb 4。

注册入口（guaranteed 路径）：
- rs: `src/operators/rs/rs_operators_init.cpp:307-485`（`initBuiltinRsOperators()`，add 表 157 项；宏表 :131-293 同样 157 项，逐 ID 一致）
- gdal: `src/operators/gdal/gdal_operators_init.cpp:25-44`（5 项）
- otb: `src/operators/otb/otb_operators_init.cpp:23-39`（4 项）
- opencv: `src/operators/opencv/opencv_operators_init.cpp:24-42`（6 项，整文件 OPENCV 门控）
- io: `src/operators/io/io_operators_init.cpp:39-68`（17 项）

registry 机制：`RSOperatorRegistry`（`src/operators/framework/rs_operator_registry.h:25`，`REGISTER_RS_OPERATOR` 宏 :80）→ 经 `installRsOperatorProvider()`（rs_operators_init.cpp:487-505）桥接进 `AtomicAlgorithmRegistry`（`RsOperatorAdapter` 逐 name 适配）。

schema 来源：每个 rs 类有自身 `schema()` override（共享 helper：`primitiveSchema`/`detectorSchema`/`referenceInputSchemaProps` 等）；例外：
- 5 个融合别名（fusion_linear/brovey/pca/ihs/gram_schmidt）共享 `RsFusionMethodOperator::schema()`（`src/operators/rs/rs_fusion_aliases.cpp:22`）
- `otb:bundle_to_perfect_sensor` 无 override，落 `RSOperator::schema()` 基类默认（`src/operators/framework/rs_operator.cpp:21`，**空 properties/outputs**）

完整 189 行明细见 §6 附录表（家族、ID、注册类、注册点 file:line、schema 来源、双层 sidecar 有无、测试直引）。

## 2. manifest / sidecar 条目统计

### 2.1 data/processing/toolbox_manifest.json（provider 工具箱清单）
- 顶层 `{version:1, categories(5), handcrafted_required(18)}`；categories = gdal_raster_basic 9 / gdal_raster_analysis 10 / gdal_vector 3 / otb_rs 10 / otb_preprocess 10 = 42 个 `required` id；与 handcrafted 18 去重后 **50 个 id**，全部 `gdal_tools:`/`otb_tools:`/`qgis_algorithms:` 前缀。
- 消费方：`tests/test_toolbox_coverage.cpp`（4 个 TEST_CASE，S3 起双向 100% 闸门）。
- provider 静态注册面：gdal_tools 26（`src/processing/providers/gdal_tools/provider.cpp:44-90`）+ otb_tools 30（`src/processing/providers/otb_tools/provider.cpp:48-106`）+ qgis_algorithms 39（`src/processing/providers/qgis_algorithms/provider.cpp:65-121`），另有 CLI auto-discovery 的 GenericCliAlgorithm 动态注册。

### 2.2 data/processing/algorithm_meta/（顶层稀疏目录，ADR 0122）
- **57 个** sidecar（56 rs + 1 `gdal:polygonize`）；`id` 与文件名 `:`→`-` **且 `_`→`-`** 规范一致（rs:apply_mask → rs-apply-mask.json；初版误记为仅 `:`→`-`，闸门初版因此对带下划线算子静默跳过——已修正并经 RED/GREEN 双向验证）；**零幽灵**（全部命中注册表）。
- 生成机制：`AlgorithmMetaStore::exportCatalog`（`src/processing/framework/algorithm_meta_store.h:90`）只导出 taskFamily 非空者；漂移闸门 `tests/test_algorithm_meta_drift.cpp`（1 case，钉死目录精确集合）；运行时 resolve 描述符优先（`algorithm_meta_store.h:71` `resolveAgainstDescriptor`，冲突字段丢弃并记 drift）。

### 2.3 data/processing/algorithm_meta/capability/（能力目录，ADR 0154/D8）
- **157 个** per-operator sidecar + `capability_relations.json`（chains 24 / exclusive 73 / grid_fixer 1 / requires_shared_grid 14，schema_version 1）。
- 集合 == 注册表 rs 全集（157/157），**零缺失零幽灵**；派生字段（io/parameters/determinism/memory）由 `capability_knowledge_tool gen-meta` 从活描述符生成；闸门：`tests/test_capability_knowledge.cpp`（15 cases，含 registry==catalog 覆盖）、`test_capability_surface_parity.cpp`（4）、`test_capability_completeness.cpp`（2）。
- 非 rs 的 32 个算子（gdal 5 / io 17 / opencv 6 / otb 4）**无 capability sidecar**（README 明示 rs-only 设计）。

## 3. 三类差异清单

### (a) manifest 声称存在但代码未注册 —— **0 条**
50 个 manifest id 全部在 provider `loadAlgorithms()` 静态可证（gdal 26/26、otb 30/30、qgis 39/39 覆盖），且有运行时双向闸门。未发现任何幻影 id。

### (b) 代码注册但 manifest/sidecar 缺失 —— 结构性 3 组（均「by design」或信息级）
1. **toolbox_manifest 缺 45 个 provider id**（信息级，P3）：gdal_tools:gdaltindex；otb 10 个（band_math_x、binary_morphological、concatenate_images、dynamic_convert、extract_roi、gray_scale_morphological、lsms、multi_resolution_pyramid、train_vector_classifier + 已列 svm…实际为 10 个未列）；qgis_algorithms 34 个（全部 raster_/vector_/native 系）。manifest 语义是「required 下限」而非全量清单，非缺陷；但 qgis 侧 34/39 未入清单，toolbox 目录可见性与清单差距最大。
2. **顶层稀疏 sidecar 缺 132 id**（设计如此）：仅 metadata() 带 `meta["task"]` 的算子导出（42 个 cpp 文件设置 task → 57 个导出）；io/opencv/otb 全家族 + gdal 4/5 + rs 101/157 无顶层条目。rs 侧无 task 的 101 个多为 grid/raster_spatial/import 类。
3. **capability 目录缺全部 32 个非 rs 算子**（README 明示 rs-only）：能力发现层（MCP list/search/get_schema 的 catalog 块）对 gdal:/io:/otb:/opencv: 不可见。P2 建议：要么在 README/响应里显式声明作用域，要么扩目录。

### (c) 参数名/类型/必选/输出描述不匹配 —— 全量静态核对了 157 个 capability sidecar
参数名与必选性：157/157 sidecar 的 parameters/inputs 名字全部可在代码 schema() 路径中定位（直接 props[]、同文件共享 helper、`referenceInputSchemaProps`（`src/operators/rs/rs_spectral_reference_input.cpp:561`，target/targetRef/refs/refsRef/endmembers/endmembersRef/libraryPath/libraryMaterials）、循环生成键 bandsA/bandsB（`rs_model_task_operators.cpp:496-506`））；抽查 4 例 required 语义（majority_filter/recode/sar_coregister/mnf_inverse）sidecar 与代码 `makeRequired` 一致。**未发现名字级幽灵参数**。

但发现一条**系统性类型缺陷**：

**D1（P1）：capability sidecar 参数行丢失 array 语义 —— 63 个 sidecar、91 个参数行。**
- 现象：`bands: integer`、`scenes: string`、`inputs: string`、`target`/`endmembers`/`refs`/`interference`/`libraryMaterials`/`provenance`/`regions`/`point`/`polygon`/`companionScenes`/`components`/`piecewisePoints`/`named_inputs` 等在代码 schema 中全部是 `type:"array"`（如 `src/operators/rs/rs_inference_operator.cpp:199-207`、`rs_model_task_operators.cpp:43-51`、`rs_kmeans_operator.cpp:103-108`、`rs_zonal_stats_operator.cpp:67-72`、`rs_sar_temporal_stats`/temporal 系 scenes），而 sidecar 里全被压平成元素类型标量，且无 `array:true`/`item_type` 字段（157 个 sidecar 的参数类型词表 {string 400, enum 143, numeric 206, integer 272, boolean 53, json 6} 中**根本不存在 array**；inputs 行 array=true 计数为 0）。
- 根因（证据）：
  - `src/agent/harness/capability_catalog.cpp:334-377` `ioRow()`：**data 行**（input/output）在 :349-353 发出 `row["array"]=true` 与 `row["item_type"]`；**parameter 行**（else 分支 :366-377）只发 type/required/default/enum，**丢弃 isArray/itemType**；
  - `src/processing/framework/atomic_algorithm_adapter.cpp:229-236`：`type:"array"` → `pDesc.isArray=true; pDesc.itemType=...; pDesc.type=pDesc.itemType`（行类型被写成元素类型）；
  - 生成器与 sidecar 同源，`gen-meta` 再生不会自愈，drift 闸门（对比 sidecar vs 再生结果）结构上看不见此缺陷。
- 影响：agent 经 `get_algorithm_schema` 读到 `scenes: string`/`bands: integer`，按标量传参；`rs_inference_operator.cpp:312`、`rs_zonal_stats_operator.cpp:128` 对非数组 bands **静默忽略走默认**，temporal 系 scenes 非数组则直接执行失败——契约与运行时行为背离，波及 63/157（40%）算子、全部 temporal(23)/推理(7)/检测(9)/镶嵌等主力链路。
- 修复建议：ioRow parameter 分支补 `array`/`item_type`（与 :349-353 对齐）+ adapter 不再把 itemType 冒充 type（或 catalog 端从 isArray 还原），regen `capability_knowledge_tool gen-meta`；加一条针对 array 参数的 parity 断言进 `test_capability_knowledge.cpp`。

**D2（P3）：`otb:bundle_to_perfect_sensor` 空 schema。** 无 `schema()` override，落 `rs_operator.cpp:21` 基类默认（空 properties/outputs），但 `otb_operator_base.cpp:80-85` `validateCommonParams` 却强制要求 `output` 字符串——schema 合同与校验行为不符（同族其余 3 个 otb 算子均有显式 schema）。

**D3（P3）：OPENCV 关闭构建会撕裂 capability 覆盖闸门。** `CMakeLists.txt:192-196` 可将 `SICNU_HAS_OPENCV` 置 FALSE，17 个 rs 算子（kmeans/supervised/obia×5/segment_stats/majority_filter/recode/infer/segment/detect/embedding/classify/change/regress）不注册，但它们的 capability sidecar 仍在目录中，`test_capability_knowledge` 的 registry==catalog 集合断言将失败（构建环境敏感，建议文档化或 CMake 级硬依赖声明）。

**非缺陷备注**：① sidecar 类型词表 normalization（enum↔enum(string)、numeric↔number）一致无碍；② `rs:band_math` outputs 含 expression/width/height 回显是代码自声明（`rs_band_math_operator.cpp:28-32`），sidecar 忠实镜像；③ rs:change_mad / rs:change_ratio 无独立测试 id 直引，但经 `rs:change_detection` facade 的 method 参数覆盖（`tests/test_change_detection.cpp` 50 处 mad/ratio 引用）。

## 4. 测试覆盖统计

- tests/ 共 **1016** 个 cpp；`tests/CMakeLists.txt` 声明 **963** 个测试目标（601 个 `test_*` executable + `sicnu_add_*` 系列 wrapper：io/sdk/study/dialog/experiment_studio/help/autonomy/mission/d17/mission_runtime/teaching/teaching_admin/explain）。
- **算子 id 直引覆盖 186/189**（98.4%）：io 17/17、opencv 6/6、otb 4/4、gdal 4/5、rs 155/157。三个无直引：`gdal:pansharpen`（间接：`tests/test_pansharpening.cpp`、`tests/surface_diff_snapshot.json`）、`rs:change_mad`、`rs:change_ratio`（facade 覆盖，见上）。
- 家族关键词测试目标数（近似分类，总 963）：spectral/detection 88、terrain 48、classification/obia 38、registration/geometry 37、temporal 25、import/io 24、sar 20、enhancement/qa/mask 18、gdal 14、fusion/mosaic 9、change 6、otb 4、**opencv 1**、capability/meta/parity 闸门 61、其余（UI/agent/平台）625。
- 奇偶闸门测试齐备：`test_toolbox_coverage.cpp`(4)、`test_algorithm_meta_drift.cpp`(1)、`test_capability_knowledge.cpp`(15)、`test_capability_surface_parity.cpp`(4)、`test_capability_completeness.cpp`(2)。
- 薄弱点：opencv 家族 6 算子仅 1 个专门测试目标；otb 4 算子 4 目标（比例尚可，绝对量少）。

## 5. 最严重 5 条（汇总）

| # | 级别 | 摘要 | 证据 |
|---|------|------|------|
| 1 | P1 | capability sidecar 参数行系统性丢 array 语义，63/157 算子 91 行契约错误 | capability_catalog.cpp:366-377 vs :349-353；atomic_algorithm_adapter.cpp:229-236 |
| 2 | P2 | agent 传标量导致 bands/scenes 被静默忽略或执行失败（D1 的行为面） | rs_inference_operator.cpp:312；rs_zonal_stats_operator.cpp:128 |
| 3 | P2 | 能力发现层对 32 个非 rs 算子（gdal/io/otb/opencv 全部）不可见，无显式作用域声明 | data/processing/algorithm_meta/README.md:41-46（设计）；catalog 无对应条目 |
| 4 | P3 | otb:bundle_to_perfect_sensor 空 schema 但运行时强校验 output | rs_operator.cpp:21；otb_operator_base.cpp:80-85 |
| 5 | P3 | OPENCV 关闭构建 → 17 个 sidecar 孤儿，capability 覆盖闸门必红 | CMakeLists.txt:192-196；rs_operators_init.cpp:466-484 门控块 |

## 6. 附录：注册 ID 全表（189）

列：家族 | ID | 注册类 | 注册点（guaranteed add 路径）| schema() 来源 | 顶层 sidecar | capability sidecar | 测试直引（tests/ 中出现该 id 字符串）
| # | 家族 | 算子 ID | 注册类 | 注册点 (guaranteed add) | schema() 来源 | 顶层 sidecar | capability sidecar | 测试直引 |
|--:|---|---|---|---|---|--:|--:|--:|
| 1 | gdal | `gdal:clip` | GdalClipOperator | src/operators/gdal/gdal_operators_init.cpp:41 | src/operators/gdal/gdal_clip_operator.cpp:13 | - | - | Y |
| 2 | gdal | `gdal:orthorectification` | GdalOrthorectificationOperator | src/operators/gdal/gdal_operators_init.cpp:39 | src/operators/gdal/gdal_orthorectification_operator.cpp:40 | - | - | Y |
| 3 | gdal | `gdal:pansharpen` | GdalPanSharpenOperator | src/operators/gdal/gdal_operators_init.cpp:43 | src/operators/gdal/gdal_pansharpen_operator.cpp:22 | - | - | - |
| 4 | gdal | `gdal:polygonize` | GdalPolygonizeOperator | src/operators/gdal/gdal_operators_init.cpp:42 | src/operators/gdal/gdal_polygonize_operator.cpp:39 | Y | - | Y |
| 5 | gdal | `gdal:reproject` | GdalReprojectOperator | src/operators/gdal/gdal_operators_init.cpp:40 | src/operators/gdal/gdal_reproject_operator.cpp:20 | - | - | Y |
| 6 | io | `io:build_overviews` | IoBuildOverviewsOperator | src/operators/io/io_operators_init.cpp:56 | src/operators/io/io_operators.cpp:541 | - | - | Y |
| 7 | io | `io:cache_prefetch` | IoCachePrefetchOperator | src/operators/io/io_operators_init.cpp:64 | src/operators/io/io_fabric_operators.cpp:322 | - | - | Y |
| 8 | io | `io:catalog_search` | IoCatalogSearchOperator | src/operators/io/io_operators_init.cpp:61 | src/operators/io/io_fabric_operators.cpp:122 | - | - | Y |
| 9 | io | `io:clip` | IoClipOperator | src/operators/io/io_operators_init.cpp:54 | src/operators/io/io_operators.cpp:358 | - | - | Y |
| 10 | io | `io:convert_format` | IoConvertFormatOperator | src/operators/io/io_operators_init.cpp:55 | src/operators/io/io_operators.cpp:457 | - | - | Y |
| 11 | io | `io:cube_plan` | IoCubePlanOperator | src/operators/io/io_operators_init.cpp:62 | src/operators/io/io_fabric_operators.cpp:182 | - | - | Y |
| 12 | io | `io:cube_window` | IoCubeWindowOperator | src/operators/io/io_operators_init.cpp:63 | src/operators/io/io_fabric_operators.cpp:226 | - | - | Y |
| 13 | io | `io:doctor` | IoDoctorOperator | src/operators/io/io_operators_init.cpp:60 | src/operators/io/io_operators.cpp:803 | - | - | Y |
| 14 | io | `io:inspect` | IoInspectOperator | src/operators/io/io_operators_init.cpp:59 | src/operators/io/io_operators.cpp:757 | - | - | Y |
| 15 | io | `io:make_cog` | IoMakeCogOperator | src/operators/io/io_operators_init.cpp:57 | src/operators/io/io_operators.cpp:600 | - | - | Y |
| 16 | io | `io:metadata_patch` | IoMetadataPatchOperator | src/operators/io/io_operators_init.cpp:66 | src/operators/io/io_operators.cpp:913 | - | - | Y |
| 17 | io | `io:reproject` | IoReprojectOperator | src/operators/io/io_operators_init.cpp:53 | src/operators/io/io_operators.cpp:289 | - | - | Y |
| 18 | io | `io:subdatasets` | IoSubdatasetsOperator | src/operators/io/io_operators_init.cpp:65 | src/operators/io/io_operators.cpp:848 | - | - | Y |
| 19 | io | `io:translate` | IoTranslateOperator | src/operators/io/io_operators_init.cpp:51 | src/operators/io/io_operators.cpp:139 | - | - | Y |
| 20 | io | `io:vector_convert` | IoVectorConvertOperator | src/operators/io/io_operators_init.cpp:58 | src/operators/io/io_operators.cpp:697 | - | - | Y |
| 21 | io | `io:verify_dataset` | IoVerifyDatasetOperator | src/operators/io/io_operators_init.cpp:67 | src/operators/io/io_operators.cpp:978 | - | - | Y |
| 22 | io | `io:warp` | IoWarpOperator | src/operators/io/io_operators_init.cpp:52 | src/operators/io/io_operators.cpp:213 | - | - | Y |
| 23 | opencv | `opencv:canny` | OpenCvCannyOperator | src/operators/opencv/opencv_operators_init.cpp:41 | src/operators/opencv/opencv_filter_operators.cpp:305 | - | - | Y |
| 24 | opencv | `opencv:gaussian_blur` | OpenCvGaussianBlurOperator | src/operators/opencv/opencv_operators_init.cpp:36 | src/operators/opencv/opencv_filter_operators.cpp:66 | - | - | Y |
| 25 | opencv | `opencv:laplacian` | OpenCvLaplacianOperator | src/operators/opencv/opencv_operators_init.cpp:40 | src/operators/opencv/opencv_filter_operators.cpp:257 | - | - | Y |
| 26 | opencv | `opencv:mean_blur` | OpenCvMeanBlurOperator | src/operators/opencv/opencv_operators_init.cpp:37 | src/operators/opencv/opencv_filter_operators.cpp:111 | - | - | Y |
| 27 | opencv | `opencv:median_blur` | OpenCvMedianBlurOperator | src/operators/opencv/opencv_operators_init.cpp:38 | src/operators/opencv/opencv_filter_operators.cpp:152 | - | - | Y |
| 28 | opencv | `opencv:sobel` | OpenCvSobelOperator | src/operators/opencv/opencv_operators_init.cpp:39 | src/operators/opencv/opencv_filter_operators.cpp:200 | - | - | Y |
| 29 | otb | `otb:bundle_to_perfect_sensor` | OtbBundleToPerfectSensorOperator | src/operators/otb/otb_operators_init.cpp:38 | rs_operator.cpp:21 (base default — EMPTY schema) | - | - | Y |
| 30 | otb | `otb:compute_images_statistics` | OtbComputeImagesStatisticsOperator | src/operators/otb/otb_operators_init.cpp:37 | src/operators/otb/otb_compute_images_statistics_operator.cpp:15 | - | - | Y |
| 31 | otb | `otb:meanshift_segmentation` | OtbSegmentationOperator | src/operators/otb/otb_operators_init.cpp:35 | src/operators/otb/otb_segmentation_operator.cpp:27 | - | - | Y |
| 32 | otb | `otb:svm_classification` | OtbSvmClassificationOperator | src/operators/otb/otb_operators_init.cpp:36 | src/operators/otb/otb_svm_classification_operator.cpp:21 | - | - | Y |
| 33 | rs | `rs:ace` | RsAceOperator | src/operators/rs/rs_operators_init.cpp:391 | src/operators/rs/rs_spectral_detection_operators.cpp:718 | Y | Y | Y |
| 34 | rs | `rs:align` | RsAlignOperator | src/operators/rs/rs_operators_init.cpp:419 | src/operators/rs/rs_grid_operators.cpp:262 | - | Y | Y |
| 35 | rs | `rs:apply_mask` | RsApplyMaskOperator | src/operators/rs/rs_operators_init.cpp:374 | src/operators/rs/rs_apply_mask_operator.cpp:123 | - | Y | Y |
| 36 | rs | `rs:atmospheric_correction` | RsAtmosphericCorrectionOperator | src/operators/rs/rs_operators_init.cpp:347 | src/operators/rs/rs_atmospheric_correction_operator.cpp:35 | - | Y | Y |
| 37 | rs | `rs:atmospheric_dos1` | RsAtmosphericDos1Operator | src/operators/rs/rs_operators_init.cpp:349 | src/operators/rs/rs_atmospheric_aliases.cpp:72 | - | Y | Y |
| 38 | rs | `rs:atmospheric_dos2` | RsAtmosphericDos2Operator | src/operators/rs/rs_operators_init.cpp:350 | src/operators/rs/rs_atmospheric_aliases.cpp:125 | - | Y | Y |
| 39 | rs | `rs:atmospheric_quac` | RsAtmosphericQuacOperator | src/operators/rs/rs_operators_init.cpp:351 | src/operators/rs/rs_atmospheric_aliases.cpp:179 | - | Y | Y |
| 40 | rs | `rs:band_math` | RsBandMathOperator | src/operators/rs/rs_operators_init.cpp:330 | src/operators/rs/rs_band_math_operator.cpp:19 | - | Y | Y |
| 41 | rs | `rs:band_ratio` | RsBandRatioOperator | src/operators/rs/rs_operators_init.cpp:331 | src/operators/rs/rs_band_tools_operators.cpp:23 | - | Y | Y |
| 42 | rs | `rs:brdf_normalization` | RsBrdfNormalizationOperator | src/operators/rs/rs_operators_init.cpp:387 | src/operators/rs/rs_brdf_normalization_operator.cpp:95 | Y | Y | Y |
| 43 | rs | `rs:cem_detection` | RsCemOperator | src/operators/rs/rs_operators_init.cpp:392 | src/operators/rs/rs_spectral_detection_operators.cpp:751 | Y | Y | Y |
| 44 | rs | `rs:change` | RsChangeOperator | src/operators/rs/rs_operators_init.cpp:482 | src/operators/rs/rs_model_task_operators.cpp:488 | Y | Y | Y |
| 45 | rs | `rs:change_cva` | RsChangeCvaOperator | src/operators/rs/rs_operators_init.cpp:357 | src/operators/rs/rs_change_primitives.cpp:333 | - | Y | Y |
| 46 | rs | `rs:change_cva_angle` | RsChangeCvaAngleOperator | src/operators/rs/rs_operators_init.cpp:358 | src/operators/rs/rs_change_primitives.cpp:399 | - | Y | Y |
| 47 | rs | `rs:change_detection` | RsChangeDetectionOperator | src/operators/rs/rs_operators_init.cpp:353 | src/operators/rs/rs_change_detection_operator.cpp:64 | Y | Y | Y |
| 48 | rs | `rs:change_difference` | RsChangeDifferenceOperator | src/operators/rs/rs_operators_init.cpp:354 | src/operators/rs/rs_change_primitives.cpp:248 | - | Y | Y |
| 49 | rs | `rs:change_irmad` | RsChangeIrMadOperator | src/operators/rs/rs_operators_init.cpp:362 | src/operators/rs/rs_change_primitives.cpp:722 | - | Y | Y |
| 50 | rs | `rs:change_log_ratio` | RsChangeLogRatioOperator | src/operators/rs/rs_operators_init.cpp:360 | src/operators/rs/rs_change_primitives.cpp:612 | Y | Y | Y |
| 51 | rs | `rs:change_mad` | RsChangeMadOperator | src/operators/rs/rs_operators_init.cpp:361 | src/operators/rs/rs_change_primitives.cpp:364 | - | Y | - |
| 52 | rs | `rs:change_normalized_difference` | RsChangeNormalizedDifferenceOperator | src/operators/rs/rs_operators_init.cpp:355 | src/operators/rs/rs_change_primitives.cpp:277 | - | Y | Y |
| 53 | rs | `rs:change_ratio` | RsChangeRatioOperator | src/operators/rs/rs_operators_init.cpp:356 | src/operators/rs/rs_change_primitives.cpp:305 | - | Y | - |
| 54 | rs | `rs:change_sam` | RsChangeSamOperator | src/operators/rs/rs_operators_init.cpp:359 | src/operators/rs/rs_change_primitives.cpp:517 | - | Y | Y |
| 55 | rs | `rs:classify` | RsClassifyOperator | src/operators/rs/rs_operators_init.cpp:481 | src/operators/rs/rs_model_task_operators.cpp:430 | Y | Y | Y |
| 56 | rs | `rs:cn_product_import` | RsCnProductImportOperator | src/operators/rs/rs_operators_init.cpp:458 | src/operators/rs/rs_cn_product_import_operator.cpp:16 | - | Y | Y |
| 57 | rs | `rs:connected_components` | RsConnectedComponentsOperator | src/operators/rs/rs_operators_init.cpp:421 | src/operators/rs/rs_raster_spatial_operators.cpp:567 | Y | Y | Y |
| 58 | rs | `rs:continuum_removal` | RsContinuumRemovalOperator | src/operators/rs/rs_operators_init.cpp:344 | src/operators/rs/rs_continuum_removal_operator.cpp:26 | - | Y | Y |
| 59 | rs | `rs:contrast_stretch` | RsContrastStretchOperator | src/operators/rs/rs_operators_init.cpp:333 | src/operators/rs/rs_band_tools_operators.cpp:236 | - | Y | Y |
| 60 | rs | `rs:detect` | RsDetectOperator | src/operators/rs/rs_operators_init.cpp:479 | src/operators/rs/rs_model_task_operators.cpp:231 | Y | Y | Y |
| 61 | rs | `rs:dn_to_radiance` | RsDnToRadianceOperator | src/operators/rs/rs_operators_init.cpp:348 | src/operators/rs/rs_atmospheric_aliases.cpp:20 | - | Y | Y |
| 62 | rs | `rs:embedding` | RsEmbeddingOperator | src/operators/rs/rs_operators_init.cpp:480 | src/operators/rs/rs_model_task_operators.cpp:322 | Y | Y | Y |
| 63 | rs | `rs:endmember_analysis` | RsEndmemberAnalysisOperator | src/operators/rs/rs_operators_init.cpp:343 | src/operators/rs/rs_endmember_analysis_operator.cpp:31 | Y | Y | Y |
| 64 | rs | `rs:endmember_extraction` | RsEndmemberExtractionOperator | src/operators/rs/rs_operators_init.cpp:346 | src/operators/rs/rs_endmember_extraction_operator.cpp:29 | - | Y | Y |
| 65 | rs | `rs:evi` | RsEviOperator | src/operators/rs/rs_operators_init.cpp:325 | src/operators/rs/rs_spectral_index_aliases.cpp:70 | - | Y | Y |
| 66 | rs | `rs:extract_bands` | RsExtractBandsOperator | src/operators/rs/rs_operators_init.cpp:332 | src/operators/rs/rs_band_tools_operators.cpp:137 | - | Y | Y |
| 67 | rs | `rs:feature_normalize` | RsFeatureNormalizeOperator | src/operators/rs/rs_operators_init.cpp:382 | src/operators/rs/rs_feature_normalize_operator.cpp:79 | - | Y | Y |
| 68 | rs | `rs:feature_select` | RsFeatureSelectOperator | src/operators/rs/rs_operators_init.cpp:383 | src/operators/rs/rs_feature_select_operator.cpp:51 | - | Y | Y |
| 69 | rs | `rs:feature_stack` | RsFeatureStackOperator | src/operators/rs/rs_operators_init.cpp:381 | src/operators/rs/rs_feature_stack_operator.cpp:56 | - | Y | Y |
| 70 | rs | `rs:fill_holes` | RsFillHolesOperator | src/operators/rs/rs_operators_init.cpp:422 | src/operators/rs/rs_raster_spatial_operators.cpp:605 | Y | Y | Y |
| 71 | rs | `rs:focal_stats` | RsFocalStatsOperator | src/operators/rs/rs_operators_init.cpp:426 | src/operators/rs/rs_raster_spatial_operators.cpp:743 | Y | Y | Y |
| 72 | rs | `rs:fusion_brovey` | RsFusionBroveyOperator | src/operators/rs/rs_operators_init.cpp:377 | src/operators/rs/rs_fusion_aliases.cpp:22 (shared RsFusionMethodOperator) | - | Y | Y |
| 73 | rs | `rs:fusion_gram_schmidt` | RsFusionGramSchmidtOperator | src/operators/rs/rs_operators_init.cpp:380 | src/operators/rs/rs_fusion_aliases.cpp:22 (shared RsFusionMethodOperator) | - | Y | Y |
| 74 | rs | `rs:fusion_ihs` | RsFusionIhsOperator | src/operators/rs/rs_operators_init.cpp:379 | src/operators/rs/rs_fusion_aliases.cpp:22 (shared RsFusionMethodOperator) | - | Y | Y |
| 75 | rs | `rs:fusion_linear` | RsFusionLinearOperator | src/operators/rs/rs_operators_init.cpp:376 | src/operators/rs/rs_fusion_aliases.cpp:22 (shared RsFusionMethodOperator) | - | Y | Y |
| 76 | rs | `rs:fusion_pca` | RsFusionPcaOperator | src/operators/rs/rs_operators_init.cpp:378 | src/operators/rs/rs_fusion_aliases.cpp:22 (shared RsFusionMethodOperator) | - | Y | Y |
| 77 | rs | `rs:gaofen_import` | RsGaofenImportOperator | src/operators/rs/rs_operators_init.cpp:459 | src/operators/rs/rs_gaofen_import_operator.cpp:17 | - | Y | Y |
| 78 | rs | `rs:hj_import` | RsHjImportOperator | src/operators/rs/rs_operators_init.cpp:461 | src/operators/rs/rs_hj_import_operator.cpp:15 | - | Y | Y |
| 79 | rs | `rs:image_enhancement` | RsImageEnhancementOperator | src/operators/rs/rs_operators_init.cpp:334 | src/operators/rs/rs_image_enhancement_operator.cpp:26 | - | Y | Y |
| 80 | rs | `rs:image_fusion` | RsImageFusionOperator | src/operators/rs/rs_operators_init.cpp:375 | src/operators/rs/rs_image_fusion_operator.cpp:30 | - | Y | Y |
| 81 | rs | `rs:infer` | RsInferenceOperator | src/operators/rs/rs_operators_init.cpp:477 | src/operators/rs/rs_inference_operator.cpp:189 | Y | Y | Y |
| 82 | rs | `rs:kmeans_classification` | RsKmeansOperator | src/operators/rs/rs_operators_init.cpp:467 | src/operators/rs/rs_kmeans_operator.cpp:84 | - | Y | Y |
| 83 | rs | `rs:landsat_import` | RsLandsatImportOperator | src/operators/rs/rs_operators_init.cpp:455 | src/operators/rs/rs_landsat_import_operator.cpp:37 | - | Y | Y |
| 84 | rs | `rs:library_select` | RsLibrarySelectOperator | src/operators/rs/rs_operators_init.cpp:431 | src/operators/rs/rs_library_select_operator.cpp:70 | - | Y | Y |
| 85 | rs | `rs:local_extrema` | RsLocalExtremaOperator | src/operators/rs/rs_operators_init.cpp:425 | src/operators/rs/rs_raster_spatial_operators.cpp:720 | Y | Y | Y |
| 86 | rs | `rs:local_rx_anomaly` | RsLocalRxOperator | src/operators/rs/rs_operators_init.cpp:340 | src/operators/rs/rs_local_rx_operator.cpp:28 | Y | Y | Y |
| 87 | rs | `rs:majority_filter` | RsMajorityFilterOperator | src/operators/rs/rs_operators_init.cpp:475 | src/operators/rs/rs_majority_filter_operator.cpp:60 | - | Y | Y |
| 88 | rs | `rs:matched_filter` | RsMatchedFilterOperator | src/operators/rs/rs_operators_init.cpp:390 | src/operators/rs/rs_spectral_detection_operators.cpp:683 | Y | Y | Y |
| 89 | rs | `rs:mndwi` | RsMndwiOperator | src/operators/rs/rs_operators_init.cpp:329 | src/operators/rs/rs_spectral_index_aliases.cpp:278 | - | Y | Y |
| 90 | rs | `rs:mnf` | RsMnfOperator | src/operators/rs/rs_operators_init.cpp:428 | src/operators/rs/rs_mnf_operator.cpp:38 | - | Y | Y |
| 91 | rs | `rs:mnf_inverse` | RsMnfInverseOperator | src/operators/rs/rs_operators_init.cpp:429 | src/operators/rs/rs_mnf_inverse_operator.cpp:393 | - | Y | Y |
| 92 | rs | `rs:modis_georeference` | RsModisGeoreferenceOperator | src/operators/rs/rs_operators_init.cpp:462 | src/operators/rs/rs_modis_georeference_operator.cpp:25 | - | Y | Y |
| 93 | rs | `rs:modis_import` | RsModisImportOperator | src/operators/rs/rs_operators_init.cpp:457 | src/operators/rs/rs_modis_import_operator.cpp:38 | - | Y | Y |
| 94 | rs | `rs:morphology` | RsMorphologyOperator | src/operators/rs/rs_operators_init.cpp:420 | src/operators/rs/rs_raster_spatial_operators.cpp:529 | Y | Y | Y |
| 95 | rs | `rs:mosaic` | RsMosaicOperator | src/operators/rs/rs_operators_init.cpp:432 | src/operators/rs/rs_mosaic_operator.cpp:77 | - | Y | Y |
| 96 | rs | `rs:ndbi` | RsNdbiOperator | src/operators/rs/rs_operators_init.cpp:328 | src/operators/rs/rs_spectral_index_aliases.cpp:227 | - | Y | Y |
| 97 | rs | `rs:ndvi` | RsNdviOperator | src/operators/rs/rs_operators_init.cpp:324 | src/operators/rs/rs_spectral_index_aliases.cpp:20 | - | Y | Y |
| 98 | rs | `rs:ndwi` | RsNdwiOperator | src/operators/rs/rs_operators_init.cpp:326 | src/operators/rs/rs_spectral_index_aliases.cpp:124 | - | Y | Y |
| 99 | rs | `rs:obia_classify` | RsObiaClassifyOperator | src/operators/rs/rs_operators_init.cpp:470 | src/operators/rs/rs_obia_classify_operator.cpp:109 | - | Y | Y |
| 100 | rs | `rs:obia_features` | RsObiaFeaturesOperator | src/operators/rs/rs_operators_init.cpp:472 | src/operators/rs/rs_obia_features_operator.cpp:31 | - | Y | Y |
| 101 | rs | `rs:obia_hierarchy` | RsObiaHierarchyOperator | src/operators/rs/rs_operators_init.cpp:471 | src/operators/rs/rs_obia_hierarchy_operator.cpp:141 | - | Y | Y |
| 102 | rs | `rs:obia_label` | RsObiaLabelOperator | src/operators/rs/rs_operators_init.cpp:473 | src/operators/rs/rs_obia_label_operator.cpp:27 | - | Y | Y |
| 103 | rs | `rs:obia_segment` | RsObiaSegmentOperator | src/operators/rs/rs_operators_init.cpp:469 | src/operators/rs/rs_obia_segment_operator.cpp:37 | - | Y | Y |
| 104 | rs | `rs:osp_detection` | RsOspOperator | src/operators/rs/rs_operators_init.cpp:394 | src/operators/rs/rs_spectral_detection_operators.cpp:836 | Y | Y | Y |
| 105 | rs | `rs:pca` | RsPcaOperator | src/operators/rs/rs_operators_init.cpp:427 | src/operators/rs/rs_pca_operator.cpp:19 | - | Y | Y |
| 106 | rs | `rs:post_classification_change` | RsPostClassificationChangeOperator | src/operators/rs/rs_operators_init.cpp:372 | src/operators/rs/rs_post_classification_change_operator.cpp:42 | - | Y | Y |
| 107 | rs | `rs:proximity` | RsProximityOperator | src/operators/rs/rs_operators_init.cpp:424 | src/operators/rs/rs_raster_spatial_operators.cpp:681 | Y | Y | Y |
| 108 | rs | `rs:qa_mask` | RsQaMaskOperator | src/operators/rs/rs_operators_init.cpp:373 | src/operators/rs/rs_qa_mask_operator.cpp:150 | Y | Y | Y |
| 109 | rs | `rs:quality_mosaic` | RsQualityMosaicOperator | src/operators/rs/rs_operators_init.cpp:433 | src/operators/rs/rs_quality_mosaic_operator.cpp:259 | - | Y | Y |
| 110 | rs | `rs:radiometric_calibration` | RsRadiometricCalibrationOperator | src/operators/rs/rs_operators_init.cpp:352 | src/operators/rs/rs_radiometric_calibration_operator.cpp:27 | - | Y | Y |
| 111 | rs | `rs:radiometric_qa` | RsRadiometricQaOperator | src/operators/rs/rs_operators_init.cpp:388 | src/operators/rs/rs_radiometric_qa_operator.cpp:54 | Y | Y | Y |
| 112 | rs | `rs:rasterize` | RsRasterizeOperator | src/operators/rs/rs_operators_init.cpp:399 | src/operators/rs/rs_rasterize_operator.cpp:35 | Y | Y | Y |
| 113 | rs | `rs:recode` | RsRecodeOperator | src/operators/rs/rs_operators_init.cpp:476 | src/operators/rs/rs_recode_operator.cpp:128 | - | Y | Y |
| 114 | rs | `rs:register_images` | RsRegisterImagesOperator | src/operators/rs/rs_operators_init.cpp:464 | src/operators/rs/rs_register_images_operator.cpp:81 | - | Y | Y |
| 115 | rs | `rs:regress` | RsRegressOperator | src/operators/rs/rs_operators_init.cpp:483 | src/operators/rs/rs_model_task_operators.cpp:604 | Y | Y | Y |
| 116 | rs | `rs:resample` | RsResampleOperator | src/operators/rs/rs_operators_init.cpp:418 | src/operators/rs/rs_grid_operators.cpp:134 | - | Y | Y |
| 117 | rs | `rs:rx_anomaly` | RsRxAnomalyOperator | src/operators/rs/rs_operators_init.cpp:337 | src/operators/rs/rs_rx_anomaly_operator.cpp:26 | - | Y | Y |
| 118 | rs | `rs:sam_classify` | RsSamClassifyOperator | src/operators/rs/rs_operators_init.cpp:335 | src/operators/rs/rs_sam_classify_operator.cpp:25 | - | Y | Y |
| 119 | rs | `rs:sar_backscatter` | RsSarBackscatterOperator | src/operators/rs/rs_operators_init.cpp:365 | src/operators/rs/rs_sar_backscatter_operator.cpp:43 | - | Y | Y |
| 120 | rs | `rs:sar_calibrate` | RsSarCalibrateOperator | src/operators/rs/rs_operators_init.cpp:364 | src/operators/rs/rs_sar_calibrate_operator.cpp:40 | - | Y | Y |
| 121 | rs | `rs:sar_change` | RsSarChangeOperator | src/operators/rs/rs_operators_init.cpp:371 | src/operators/rs/rs_sar_change_operator.cpp:49 | - | Y | Y |
| 122 | rs | `rs:sar_coregister` | RsSarCoregisterOperator | src/operators/rs/rs_operators_init.cpp:406 | src/operators/rs/rs_sar_coregister_operator.cpp:47 | Y | Y | Y |
| 123 | rs | `rs:sar_coregister_local` | RsSarCoregisterLocalOperator | src/operators/rs/rs_operators_init.cpp:409 | src/operators/rs/rs_sar_coregister_local_operator.cpp:52 | Y | Y | Y |
| 124 | rs | `rs:sar_displacement` | RsSarDisplacementOperator | src/operators/rs/rs_operators_init.cpp:405 | src/operators/rs/rs_sar_displacement_operator.cpp:42 | Y | Y | Y |
| 125 | rs | `rs:sar_dualpol_features` | RsSarDualPolOperator | src/operators/rs/rs_operators_init.cpp:396 | src/operators/rs/rs_sar_dualpol_operator.cpp:38 | Y | Y | Y |
| 126 | rs | `rs:sar_geocode` | RsSarGeocodeOperator | src/operators/rs/rs_operators_init.cpp:398 | src/operators/rs/rs_sar_geocode_operator.cpp:78 | Y | Y | Y |
| 127 | rs | `rs:sar_interferogram` | RsSarInterferogramOperator | src/operators/rs/rs_operators_init.cpp:402 | src/operators/rs/rs_sar_interferogram_operator.cpp:58 | Y | Y | Y |
| 128 | rs | `rs:sar_network_inversion` | RsSarNetworkInversionOperator | src/operators/rs/rs_operators_init.cpp:411 | src/operators/rs/rs_sar_network_inversion_operator.cpp:43 | Y | Y | Y |
| 129 | rs | `rs:sar_pair_network` | RsSarPairNetworkOperator | src/operators/rs/rs_operators_init.cpp:410 | src/operators/rs/rs_sar_pair_network_operator.cpp:127 | Y | Y | Y |
| 130 | rs | `rs:sar_phase_filter` | RsSarPhaseFilterOperator | src/operators/rs/rs_operators_init.cpp:403 | src/operators/rs/rs_sar_phase_filter_operator.cpp:41 | Y | Y | Y |
| 131 | rs | `rs:sar_polsar_decompose` | RsSarPolsarDecomposeOperator | src/operators/rs/rs_operators_init.cpp:401 | src/operators/rs/rs_sar_polsar_decompose_operator.cpp:157 | Y | Y | Y |
| 132 | rs | `rs:sar_ratio` | RsSarRatioOperator | src/operators/rs/rs_operators_init.cpp:369 | src/operators/rs/rs_sar_ratio_operator.cpp:42 | - | Y | Y |
| 133 | rs | `rs:sar_remove_topographic_phase` | RsSarRemoveTopographicPhaseOperator | src/operators/rs/rs_operators_init.cpp:408 | src/operators/rs/rs_sar_remove_topographic_phase_operator.cpp:90 | Y | Y | Y |
| 134 | rs | `rs:sar_speckle` | RsSarSpeckleOperator | src/operators/rs/rs_operators_init.cpp:368 | src/operators/rs/rs_sar_speckle_operator.cpp:44 | - | Y | Y |
| 135 | rs | `rs:sar_temporal_events` | RsSarTemporalEventsOperator | src/operators/rs/rs_operators_init.cpp:407 | src/operators/rs/rs_sar_temporal_events_operator.cpp:50 | Y | Y | Y |
| 136 | rs | `rs:sar_temporal_stats` | RsSarTemporalStatsOperator | src/operators/rs/rs_operators_init.cpp:400 | src/operators/rs/rs_sar_temporal_stats_operator.cpp:48 | Y | Y | Y |
| 137 | rs | `rs:sar_terrain_correction` | RsSarTerrainCorrectionOperator | src/operators/rs/rs_operators_init.cpp:367 | src/operators/rs/rs_sar_terrain_correction_operator.cpp:48 | - | Y | Y |
| 138 | rs | `rs:sar_terrain_flatten` | RsSarTerrainFlattenOperator | src/operators/rs/rs_operators_init.cpp:366 | src/operators/rs/rs_sar_terrain_flatten_operator.cpp:47 | - | Y | Y |
| 139 | rs | `rs:sar_terrain_masks` | RsSarTerrainMasksOperator | src/operators/rs/rs_operators_init.cpp:397 | src/operators/rs/rs_sar_terrain_masks_operator.cpp:71 | Y | Y | Y |
| 140 | rs | `rs:sar_texture` | RsSarTextureOperator | src/operators/rs/rs_operators_init.cpp:370 | src/operators/rs/rs_sar_texture_operator.cpp:49 | - | Y | Y |
| 141 | rs | `rs:sar_unwrap` | RsSarUnwrapOperator | src/operators/rs/rs_operators_init.cpp:404 | src/operators/rs/rs_sar_unwrap_operator.cpp:43 | Y | Y | Y |
| 142 | rs | `rs:savi` | RsSaviOperator | src/operators/rs/rs_operators_init.cpp:327 | src/operators/rs/rs_spectral_index_aliases.cpp:174 | - | Y | Y |
| 143 | rs | `rs:segment` | RsSegmentOperator | src/operators/rs/rs_operators_init.cpp:478 | src/operators/rs/rs_model_task_operators.cpp:165 | Y | Y | Y |
| 144 | rs | `rs:segment_stats` | RsSegmentStatsOperator | src/operators/rs/rs_operators_init.cpp:474 | src/operators/rs/rs_segment_stats_operator.cpp:29 | - | Y | Y |
| 145 | rs | `rs:sentinel2_import` | RsSentinel2ImportOperator | src/operators/rs/rs_operators_init.cpp:456 | src/operators/rs/rs_sentinel2_import_operator.cpp:42 | - | Y | Y |
| 146 | rs | `rs:sieve` | RsSieveOperator | src/operators/rs/rs_operators_init.cpp:423 | src/operators/rs/rs_raster_spatial_operators.cpp:643 | Y | Y | Y |
| 147 | rs | `rs:solar_geometry` | RsSolarGeometryOperator | src/operators/rs/rs_operators_init.cpp:386 | src/operators/rs/rs_solar_geometry_operator.cpp:34 | Y | Y | Y |
| 148 | rs | `rs:sparse_unmixing` | RsSparseUnmixingOperator | src/operators/rs/rs_operators_init.cpp:341 | src/operators/rs/rs_sparse_unmixing_operator.cpp:26 | Y | Y | Y |
| 149 | rs | `rs:spectral_band_select` | RsSpectralBandSelectOperator | src/operators/rs/rs_operators_init.cpp:430 | src/operators/rs/rs_spectral_band_select_operator.cpp:66 | - | Y | Y |
| 150 | rs | `rs:spectral_derivative` | RsSpectralDerivativeOperator | src/operators/rs/rs_operators_init.cpp:389 | src/operators/rs/rs_spectral_derivative_operator.cpp:96 | Y | Y | Y |
| 151 | rs | `rs:spectral_index` | RsSpectralIndexOperator | src/operators/rs/rs_operators_init.cpp:323 | src/operators/rs/rs_spectral_index_operator.cpp:59 | Y | Y | Y |
| 152 | rs | `rs:spectral_resample` | RsSpectralResampleOperator | src/operators/rs/rs_operators_init.cpp:345 | src/operators/rs/rs_spectral_resample_operator.cpp:47 | - | Y | Y |
| 153 | rs | `rs:spectral_similarity` | RsSpectralSimilarityOperator | src/operators/rs/rs_operators_init.cpp:342 | src/operators/rs/rs_spectral_similarity_operator.cpp:29 | Y | Y | Y |
| 154 | rs | `rs:spectral_spatial_fuse` | RsSpectralSpatialFuseOperator | src/operators/rs/rs_operators_init.cpp:395 | src/operators/rs/rs_spectral_spatial_fuse_operator.cpp:48 | Y | Y | Y |
| 155 | rs | `rs:spectral_unmixing` | RsSpectralUnmixingOperator | src/operators/rs/rs_operators_init.cpp:336 | src/operators/rs/rs_spectral_unmixing_operator.cpp:28 | - | Y | Y |
| 156 | rs | `rs:stack_register` | RsStackRegisterOperator | src/operators/rs/rs_operators_init.cpp:465 | src/operators/rs/rs_stack_register_operator.cpp:69 | - | Y | Y |
| 157 | rs | `rs:supervised_classification` | RsSupervisedClassificationOperator | src/operators/rs/rs_operators_init.cpp:468 | src/operators/rs/rs_supervised_classification_operator.cpp:110 | Y | Y | Y |
| 158 | rs | `rs:tcimf_detection` | RsTcimfOperator | src/operators/rs/rs_operators_init.cpp:393 | src/operators/rs/rs_spectral_detection_operators.cpp:790 | Y | Y | Y |
| 159 | rs | `rs:temporal_anomaly` | RsTemporalAnomalyOperator | src/operators/rs/rs_operators_init.cpp:445 | src/operators/rs/rs_temporal_anomaly_operator.cpp:46 | - | Y | Y |
| 160 | rs | `rs:temporal_breakpoints` | RsTemporalBreakpointsOperator | src/operators/rs/rs_operators_init.cpp:442 | src/operators/rs/rs_temporal_breakpoints_operator.cpp:55 | - | Y | Y |
| 161 | rs | `rs:temporal_composite` | RsTemporalCompositeOperator | src/operators/rs/rs_operators_init.cpp:435 | src/operators/rs/rs_temporal_composite_operator.cpp:150 | - | Y | Y |
| 162 | rs | `rs:temporal_decompose` | RsTemporalDecomposeOperator | src/operators/rs/rs_operators_init.cpp:444 | src/operators/rs/rs_temporal_decompose_operator.cpp:111 | - | Y | Y |
| 163 | rs | `rs:temporal_extract_regions` | RsTemporalExtractRegionsOperator | src/operators/rs/rs_operators_init.cpp:448 | src/operators/rs/rs_temporal_extract_regions_operator.cpp:86 | - | Y | Y |
| 164 | rs | `rs:temporal_extract_series` | RsTemporalExtractSeriesOperator | src/operators/rs/rs_operators_init.cpp:446 | src/operators/rs/rs_temporal_extract_series_operator.cpp:90 | - | Y | Y |
| 165 | rs | `rs:temporal_gap_fill` | RsTemporalGapFillOperator | src/operators/rs/rs_operators_init.cpp:439 | src/operators/rs/rs_temporal_gap_fill_operator.cpp:59 | - | Y | Y |
| 166 | rs | `rs:temporal_harmonic_breaks` | RsTemporalHarmonicBreaksOperator | src/operators/rs/rs_operators_init.cpp:450 | src/operators/rs/rs_temporal_harmonic_breaks_operator.cpp:57 | - | Y | Y |
| 167 | rs | `rs:temporal_harmonic_fit` | RsTemporalHarmonicFitOperator | src/operators/rs/rs_operators_init.cpp:440 | src/operators/rs/rs_temporal_harmonic_fit_operator.cpp:64 | - | Y | Y |
| 168 | rs | `rs:temporal_index_series` | RsTemporalIndexSeriesOperator | src/operators/rs/rs_operators_init.cpp:436 | src/operators/rs/rs_temporal_index_operator.cpp:109 | - | Y | Y |
| 169 | rs | `rs:temporal_model_select` | RsTemporalModelSelectOperator | src/operators/rs/rs_operators_init.cpp:452 | src/operators/rs/rs_temporal_model_select_operator.cpp:52 | - | Y | Y |
| 170 | rs | `rs:temporal_monitor` | RsTemporalMonitorOperator | src/operators/rs/rs_operators_init.cpp:413 | src/operators/rs/rs_temporal_monitor_operator.cpp:55 | Y | Y | Y |
| 171 | rs | `rs:temporal_phenology` | RsTemporalPhenologyOperator | src/operators/rs/rs_operators_init.cpp:441 | src/operators/rs/rs_temporal_phenology_operator.cpp:69 | - | Y | Y |
| 172 | rs | `rs:temporal_phenology_multi` | RsTemporalPhenologyMultiOperator | src/operators/rs/rs_operators_init.cpp:453 | src/operators/rs/rs_temporal_phenology_multi_operator.cpp:74 | - | Y | Y |
| 173 | rs | `rs:temporal_region_features` | RsTemporalRegionFeaturesOperator | src/operators/rs/rs_operators_init.cpp:454 | src/operators/rs/rs_temporal_region_features_operator.cpp:108 | - | Y | Y |
| 174 | rs | `rs:temporal_regularize` | RsTemporalRegularizeOperator | src/operators/rs/rs_operators_init.cpp:449 | src/operators/rs/rs_temporal_regularize_operator.cpp:62 | - | Y | Y |
| 175 | rs | `rs:temporal_sar_fusion` | RsTemporalSarFusionOperator | src/operators/rs/rs_operators_init.cpp:447 | src/operators/rs/rs_temporal_sar_fusion_operator.cpp:45 | - | Y | Y |
| 176 | rs | `rs:temporal_seasonal_breaks` | RsTemporalSeasonalBreaksOperator | src/operators/rs/rs_operators_init.cpp:451 | src/operators/rs/rs_temporal_seasonal_breaks_operator.cpp:82 | - | Y | Y |
| 177 | rs | `rs:temporal_sen_trend` | RsTemporalSenTrendOperator | src/operators/rs/rs_operators_init.cpp:443 | src/operators/rs/rs_temporal_sen_trend_operator.cpp:52 | - | Y | Y |
| 178 | rs | `rs:temporal_smooth` | RsTemporalSmoothOperator | src/operators/rs/rs_operators_init.cpp:438 | src/operators/rs/rs_temporal_smooth_operator.cpp:103 | - | Y | Y |
| 179 | rs | `rs:temporal_summary` | RsTemporalSummaryOperator | src/operators/rs/rs_operators_init.cpp:434 | src/operators/rs/rs_temporal_summary_operator.cpp:66 | - | Y | Y |
| 180 | rs | `rs:temporal_trend` | RsTemporalTrendOperator | src/operators/rs/rs_operators_init.cpp:437 | src/operators/rs/rs_temporal_trend_operator.cpp:46 | - | Y | Y |
| 181 | rs | `rs:terrain_analysis` | RsTerrainAnalysisOperator | src/operators/rs/rs_operators_init.cpp:384 | src/operators/rs/rs_terrain_analysis_operator.cpp:39 | - | Y | Y |
| 182 | rs | `rs:terrain_flow` | RsTerrainFlowOperator | src/operators/rs/rs_operators_init.cpp:414 | src/operators/rs/rs_terrain_flow_operator.cpp:46 | Y | Y | Y |
| 183 | rs | `rs:terrain_landform` | RsTerrainLandformOperator | src/operators/rs/rs_operators_init.cpp:417 | src/operators/rs/rs_terrain_landform_operator.cpp:73 | Y | Y | Y |
| 184 | rs | `rs:terrain_solar` | RsTerrainSolarOperator | src/operators/rs/rs_operators_init.cpp:416 | src/operators/rs/rs_terrain_solar_operator.cpp:96 | Y | Y | Y |
| 185 | rs | `rs:terrain_viewshed` | RsTerrainViewshedOperator | src/operators/rs/rs_operators_init.cpp:415 | src/operators/rs/rs_terrain_viewshed_operator.cpp:77 | Y | Y | Y |
| 186 | rs | `rs:threshold_raster` | RsThresholdRasterOperator | src/operators/rs/rs_operators_init.cpp:363 | src/operators/rs/rs_threshold_raster_operator.cpp:34 | - | Y | Y |
| 187 | rs | `rs:topographic_correction` | RsTopographicCorrectionOperator | src/operators/rs/rs_operators_init.cpp:385 | src/operators/rs/rs_topographic_correction_operator.cpp:130 | Y | Y | Y |
| 188 | rs | `rs:zonal_stats` | RsZonalStatsOperator | src/operators/rs/rs_operators_init.cpp:412 | src/operators/rs/rs_zonal_stats_operator.cpp:60 | Y | Y | Y |
| 189 | rs | `rs:zy3_import` | RsZy3ImportOperator | src/operators/rs/rs_operators_init.cpp:460 | src/operators/rs/rs_zy3_import_operator.cpp:15 | - | Y | Y |
