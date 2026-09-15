# CURRENT_ARCHITECTURE — mosaic/fusion 域 authority/seam 图（Phase 0 实测）

## 数据流（镶嵌，master 现状）

```
输入 GeoTIFF[] ──► RsMosaicOperator::run (rs:mosaic)
                    ├─ GdalDatasetWrapper.open / bandNoDataValue     [processing/gdal seam]
                    ├─ 护栏: CRS isSameCrs(OGR) / 像元尺寸/旋转/Y向/子像元偏移
                    ├─ union extent + 200MP cap + 原子输出清理(OutputFileCleaner)
                    └─ 512² 窗口循环: readBandWindow → 后输入覆盖前输入(valid) → writeBandWindow
                                    + context.throwIfCancelled / reportProgress
GUI: src/app/dialogs/mosaic_dialog.cpp ──(既有路径)──► 面板/算子
Legacy kernel: Mosaic::merge（数组级 last-wins，仅测试/面板参考）
```

## 数据流（融合，master 现状）

```
pan + MS ──► rs:image_fusion 算子 ──► ImageFusion::{linearWeighted,brovey,pcaFusion,ihsFusion,gramSchmidtFusion}
                                      （histogramMatch 到 pan；processNativeFusion 文件级、512² tile）
           ──► rs::algorithms::PanSharpening::{sharpen(GS/Brovey/IHS/HPF), evaluateQuality(ERGAS/CC/RMSE/SSIM)}
                （D14/ADR 0159；Wald protocol 由调用方降采样后调 evaluateQuality）
           ──► rs:fusion_* 别名（rs_fusion_aliases.cpp）
```

## authority 边界

| Authority | 归属 |
|---|---|
| 算子注册/工厂 | `rs_operators_init.cpp`（REGISTER_RS_OPERATOR 宏 + factory list）唯一注册点 |
| 能力目录 | `src/agent/harness/capability_catalog.cpp`（id → domain 表） |
| 科学契约 | `src/contracts/scientific_contract.cpp`（每 operatorId 一行 contract） |
| GDAL I/O | `processing/gdal/gdal_dataset_wrapper.{h,cpp}`（读窗/写窗/建库；无 creation options —— tiled/COG 输出经 GDAL API 由 wrapper.dataset() 原生句柄补充） |
| 重投影 | `src/operators/gdal/gdal_reproject_operator.*` + `gdal_operator_utils` GDALWarp seam（已存在，本 track 不复制） |
| 失败语义 | `RSOperatorError(ErrorCode)` fail-closed + OutputFileCleaner 原子清理 |
| 取消/进度 | `RSOperatorContext::{throwIfCancelled,reportProgress,logWarning}` |

## 缺口（本 track 填补）

1. **A**：无 scene graph/grid plan（extent/分辨率/CRS/priority/NoData/overlap inventory）；跨 CRS 仅"硬报错"，无显式预处理指引/足迹变换分析。
2. **B**：无 overlap 辐射统计、无 gain/bias 归一化、无 reference 选择、无异常拒绝。
3. **C**：无 seamline（成本面/DP 路径/云/边缘/梯度惩罚/确定性 tie-break）。
4. **D**：无 feather/多尺度金字塔融合；接缝处无加权过渡（现状=硬边界覆盖）。
5. **E**：无 quality score composite、无 per-pixel provenance/scene index 输出。
6. **F**：`evaluateQuality` 只有内存指标；无 quality report artifact（JSON）、无 Q/RASE、无 spectral distortion 防护 gate；算子面无 GS/HPF 方法。
7. **G**：输出无 overview/pyramid/COG-friendly publication；无 quality/manifest sidecar 的原子写。
8. **H**：无 mosaic 合成语料（已知 gain/云/seam ground truth）、无 100k 逻辑 tile 规模证据、无 cancel 语义测试。

## 本 track 新架构（落点）

```
sicnu_processing (rs::mosaic namespace, 新文件)
  mosaic_plan        — A: inventory/grid plan/overlap 图（OGRCoordinateTransformation 足迹统一到 plan CRS）
  mosaic_balancing   — B: overlap 统计 → 稳健 gain/bias（overlap 图 BFS 链式传播）→ 异常拒绝
  mosaic_seamline    — C: 成本面(|Δ|+|∇|+cloud+edge-distance) → DP 最小成本路径 → cut label
  mosaic_blend       — D: feather 权重 + 窗口化多尺度(Laplacian)融合，有界内存
  mosaic_quality     — E: score composite(cloud/quality/time/view) + provenance band + 贡献统计
  fusion_quality_report — F: Q/RASE/ERGAS/CC/SSIM → JSON report + 失真防护判定
sicnu_operators
  rs:quality_mosaic  — 新算子：A–E+G 编排（流式窗口、原子输出、overview、sidecar、cancel）
  rs:image_fusion    — 既有算子扩展：gram_schmidt/hpf 方法 + qualityReport 输出（F 的 surface）
```

不建立第二 scheduler/第二 registry：`rs:quality_mosaic` 走既有 RSOperator 注册/capability/contract seams。
