# Hardening 16/20 — optical-analysis-processing · Recon 现状矩阵

- 分支: `hardening/optical-analysis-processing`,基线 `origin/master = a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01`
- 启动时 master 状态与 recon seed 一致(2026-09-23 fetch 后无变化)。
- Open issues: 0。Open PRs: #1237–#1244。

## 1. 去重(与 open PR / 远端分支)

| PR/分支 | 领域 | 与本 track 的边界处理 |
| --- | --- | --- |
| #1243 hardening/spectral-hyperspectral | `spectral_resampling`、`rs_spectral_reference_input`、detection streaming | 不碰这 4 个文件;`spectral_indices.cpp`/`band_math` 不在其 file list 中,仍属本 track |
| #1244 hardening/temporal-change-phenology | temporal_* 全家 + phenology | 完全不碰 temporal |
| #1242 hardening/geo-fabric-cache-io | fabric/catalog/STAC/raster_reader | 不碰 geospatial/fabric、raster_reader |
| #1237 feat/undergrad-lab-cockpit | teaching/shell UI | 不碰 src/teaching、src/app |
| #1238–#1241 feat 分支 | 其他产品方向(agent_ops/science_context/teaching_admin/experiment-studio) | 无交集 |
| `agent/flash-processing-atomic-errors` | 旧基线;写失败可见性/取消清理 | 原子写/取消语义已被 #617/#1216/#1224 (atomic_fs) 取代;仅作缺陷线索矿,不移植 |
| `rs14-unified-verifier` | 旧平行 verifier | 禁止复活;权威是 src/verify |

## 2. 现状矩阵(逐组件)

| 组件 | 权威数据源 | 调用者 | 错误模型 | 资源上界 | 现有测试 | 已知历史修复 | 剩余疑点 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `RadiometricCalibration` (radiometric_calibration.cpp, 954L) | MTL/MTD/GDAL scale+offset | rs_radiometric_calibration_operator, DOS 算子, satellite_products | fail-closed:identity 默认拒绝(#301), 缺 SUN_ELEVATION 拒绝(#654), VCID 带失败关闭(#699) | 流式 256² tile,#444 哨兵 | test_radiometric_calibration(33 cases), test_radiometric_state, test_radiometric_transition, test_d13 e2e | #301/#444/#654/#699 | 元数据解析健壮;无明显新疑点 |
| `exp_radiometric::RadiometricCalibrator` (typed seam) | 同上 | D13 workbench | NoData 直通;非物理→NoData | O(1) | test_radiometric_calibration | #654 等 | OK |
| `AtmosphericCorrection` (atmospheric_correction.cpp, 820L) | 流式 GDAL + DarkObjectStats/StreamingPercentiles | rs_atmospheric_correction_operator(+aliases), fast_6s_lookup 上层 | fail-closed: 哨兵精确 float 比较(#699), NaN NoData 声明(#675), 截断删除(#617) | O(tile) 流式 + O(bins) 直方图 | test_atmospheric(40), test_fast_6s_atmospheric, test_atmospheric_provider | #610/#617/#632/#634/#675/#699 | processFile statsPass 失败时 errorMessage 可能为空串(P3 消息质量) |
| `SpectralIndices` legacy + `exp_spectral::SpectralIndices` typed seam | band buffers | rs_spectral_index_operator(+aliases), raster_ndvi(QGIS provider), temporal_index | safeDiv 0→NaN;NaN 传播;#680 双 regime 常量 | O(1)/像素 | test_spectral_index_*, test_band_math | #680 | OK |
| `BandMath` AST/SIMD (band_math*.cpp) | 表达式 → bytecode | rs_band_math_operator, GUI | 解析失败拒绝;depth 上限(#613);SIMD/scalar 差分 parity | chunk 256, O(regs*chunk) | test_band_math(差分 oracle + parser 边界) | #613/#700 | 差分 oracle 已强;ARM 路径仅在 OpenMP kernel |
| `ImageFusion` in-memory kernels (linear/brovey/pca/ihs/gramSchmidt) | buffers | 测试;GUI 面板(仅文档引用) | ==nodata(+大多 isnan) | O(W·H·bands) | test_image_fusion, test_image_enhancement_panel_ihs | #328/#611/#670/#677 | **ihsFusion 缺 isnan:NaN 输入→rgbToIhs→std::max(0,NaN)→0(黑),与兄弟内核+流式路径不一致(P2,确认)** |
| `ImageFusion::processNativeFusionImpl` 流式 (512² tile) | GDAL 双数据集 | rs_image_fusion_operator, rs_fusion_aliases | grid preflight(#445), 输出守卫(#617), NaN 全防护 | O(tile·bands) | test_image_fusion, test_perf_fusion, fusion quality | #445/#611/#617/#670/#700 | quality report 失败时输出已保留但 operator 报错(P3) |
| `RadiometricQa` (rs_radiometric_qa_operator) | 流式 BIP | agent/GUI | grid 兼容拒绝;64-bit 统计 | O(tile·bands) | test_radiometric_qa | 近期加固 | OK |
| OBIA segmentation (rs_obia_* + analysis/segmentation) | RsSimpleSegmenter / OTB CLI | obia_* 算子链 | engine=otb fail-closed;auto 降级告警 | FullRaster 已在 executionEstimate 披露 | test_obia_*(6 files), test_simple_segmenter | ADR 0054/0058/0060 | OK(track 已披露内存策略) |
| Supervised classification (rs_classification_pipeline, 1407L) | OpenCV backends + sidecar | rs_supervised_classification_operator, GUI | #410 Hungarian 标签映射,#1056 64-bit tile,#1219 labels sidecar fail-closed | tile 256 流式 | test_classifier_*(10+), test_classification_pipeline | #410/#1056/#1219 | OK |
| `sicnu::features::FeatureCube` (feature_cube.cpp) | GDAL metadata + sidecar spill | rs_feature_stack_operator, inference | version gate;重复 id 拒绝 | 60KB metadata 门限 | test_feature_cube | — | OK |
| `resample/align`: geometric_transform, tps, register/stack_register | GCP + GDAL warp | rs_register_images_operator 等 | — | — | test_geometric_transform, test_tps_interpolator, test_d14 e2e | — | #1242 不含;本 round 未深挖(记入未做) |

## 3. 启动事实(Phase 0)

- `git fetch --all --prune` 后 origin/master 仍为 `a9dc33fa7`;HEAD 与 merge-base 一致。
- open PR #1237–#1244;无 open issue。
- 本 worktree: `../exp-rs-hardening-optical-analysis-processing`,分支 `hardening/optical-analysis-processing`。

## 4. 确认缺陷(本轮 oracle 目标)

1. **[P2→修复] `ImageFusion::ihsFusion` NaN NoData→黑像素**:
   `src/processing/algorithms/image_fusion.cpp` 三个循环(intensity/H-S/回写)只做 `== nodata` 比较;
   NaN(自 #699/#675 起校准/QUAC 输出的 NoData 约定)不等于任何 sentinel,流入
   `rgbToIhs`(i=NaN, h=NaN, s=NaN),`ihsToRgb` 输出 NaN,最后 `std::max(0.0f, NaN)` 按
   IEEE/`std::max` 语义返回 **0.0f** —— NaN 像素变成合法黑像素(静默数据污染),与
   linearWeighted/brovey/pcaFusion/gramSchmidtFusion 及流式 IHS 路径(全部显式 isnan 防护)不一致。
   无声明 sentinel 时(nodata=NaN)同样命中:NaN==NaN 恒 false。
   Oracle: 4×4 RGB+pan,注入 NaN 孔洞,断言输出孔洞 = nodata(sentinel 与 NaN 两种约定)。

## 5. 本轮未做事项(分类)

- 已被其他 track 拥有: spectral_resampling/detection(#1243)、temporal(#1244)、fabric/raster_reader/STAC(#1242)、teaching shell(#1237)。
- 无法复现/已被取代: flash-processing-atomic-errors 的写可见性问题(已被 #617/#1216 atomic_fs 体系覆盖)。
- 需要真实平台环境: OTB CLI 实测(engine=otb 路径依赖 SICNU_OTB_PATH,单测已用 fake/stub 覆盖契约)。
- 明确未来方向: H-2 endmember JSON 管道消费、H-3 inverse MNF 等 ISSUES.md 登记项(属产品缺口,非缺陷)。
