# CURRENT_ARCHITECTURE — geometric/registration domain @ master a5b11b7f10

## Authority 图

```
rs::algorithms (src/processing, sicnu_processing)          ← 数值算法 authority（无 Qt GUI 依赖、无 OpenCV）
  geometric_transform.*   7 种闭式变换模型 + RMSE + κ 门限
  tps_interpolator.*      TPS（affine 基 + U(r)=r²lnr，λ 正则）
  gcp_manager.*           GCP aggregate（CRUD/残差/RMSE/Clark-Evans/Delaunay/CSV/JSON）
  feature_matcher.*       网格 kp + SIFT-like/ORB-like + Lowe ratio + RANSAC 单应（mt19937(42)）
  resampler.*             NN/bilinear/cubic/Lanczos + reverse-map warp + NoData 权重
  pansharpening.*         GS/Brovey/IHS/HPF + Wald 指标
  sar/*                   SAR 元数据/复数/轨道契约（F01 所有；只读消费）
QGIS georeferencing (src/analysis, 独立 authority)          ← 产品级 georeferencer（vendored QGIS + SICNU 增量）
  qgsgcptransformer.*     TransformMethod 枚举 + setRpcOptions/setDestinationCrs seam（ADR 0057）
  qgsrpcgcptransformer.*  GDAL RPC V2 + DEM + 常数 z-offset + 常数 lon/lat 中位数 bias 精化
  qgsleastsquares.*       linear/helmert/projective（GSL，可缺省）
App georeferencer (src/app/georeferencer)                   ← 交互 UI（QGIS canvas）
  qgsgeoref_shell_window  双画布 shell；mapPickToLayerCrs（#1005 fail-open 点）
  rs_georef_*             mode toggle/params panel/task list/flowchart/sift/template matcher/warp task
  rs_georeferencing_session  GCP/session/warp snapshot + Task Center dispatch
src/app/workbench/georef_dual_window.*                      ← D18 已 mount 的双窗 workbench
src/agent/tools/geometric_tool.*                            ← spatial:geometric_registration（编译但未注册，缺口）
src/operators/rs/                                           ← rs:sar_coregister / rs:align / rs:modis_georeference
```

## 稳定 seam（本 track 消费，不修改其语义）

1. `GeometricTransform::fit(model, points)` / `TransformResult{rmseF/B, κ}` — 模型选择器直接复用。
2. `FeatureMatcher::estimateHomographyRansac` — 公开 API（feature_matcher.h），粗到精终筛复用。
3. `GcpManager`（残差/分布指标）— quality 层复用。
4. `Resampler::warpRaster` — stack/warp 产物复用（不重写 warp）。
5. `QgsGcpTransformerInterface::setRpcOptions(...)` — RPC bias 升级走既有 seam，additive 参数语义。
6. Operator 三点注册：`REGISTER_RS_OPERATOR` + `initBuiltinRsOperators()` 显式 `add()` + CMakeLists。
7. Agent tool：`SpatialTool` 子类 + `registerBuiltinTools()` 注册 → `SpatialToolProvider` 进 catalog。

## 本 track 新增层

```
rs::registration (src/processing/algorithms/registration/, sicnu_processing)
  registration_types.*    契约/数据模型（本轮新增 authority）
  fft2d.*                 确定性 2D FFT 工具
  multimodal_matcher.*    A/B：phase corr + 多模态描述子 + 金字塔 + coverage
  model_selector.*        C：证据驱动模型选择（CV + 改进门限）
  rpc_bias_model.*        D：RPC bias 数学层（constant/affine、高度敏感性）
  stack_registrator.*     E：pair graph + 全局 adjustment + 闭环
  registration_quality.*  F：CE90/残差场/局部置信度/report
```
