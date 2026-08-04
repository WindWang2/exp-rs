# Wayfinder Map: 面向对象图像分析 (OBIA) 模块架构与流程深化

## Destination

构建生产级、内聚且零断层的 OBIA (面向对象图像分析) 模块：统一预设流程模板/RSOperator/TaskCenter 算子 ID，扩展 GLCM 纹理与几何形状特征，深化多尺度层次分割 (Hierarchy)，并打通 GUI 交互与 TaskCenter 异步流。

## Notes

- **Domain**: Remote Sensing Object-Based Image Analysis (Segmentation, Object Feature Extraction, Object Classification, Multi-Level Hierarchy).
- **Relevant Skills**: `/wayfinder`, `/codebase-design`, `/domain-modeling`, `/tdd`, `/qt-cpp-review`.
- **Architectural Seams**:
  - `RsObiaMainWindow` (`src/app/obia/`)
  - `RsSegmentMap`, `RsSegmentFeatures`, `RsObjectHierarchy` (`src/analysis/segmentation/`)
  - `RsObiaSegmentOperator`, `RsObiaClassifyOperator`, `RsObiaHierarchyOperator` (`src/operators/rs/`)
  - `PresetCatalogWidget`, `builtin_definitions.cpp` (`src/app/workflow/`, `src/workflow/`)

## Decisions so far

- [01 — Unify OBIA Operator IDs & Workflow Registry Contracts](issues/01-unify-obia-operator-ids.md) — Unified PresetCatalogWidget operator IDs to `rs:obia_segment` & `rs:obia_classify` and added DAG input binding.
- [02 — Expand GLCM Texture & Geometric Shape Descriptors](issues/02-expand-glcm-and-shape-features.md) — Added 4 Haralick GLCM texture metrics and 3 extended shape descriptors to `RsSegmentFeatures` ($N_{\text{features}} = 8B + 6$).
- [03 — Multi-Band Segmenter Deepening & Hierarchy Fallback Seams](issues/03-multi-band-segmenter-and-hierarchy-seam.md) — Deepened `RsSimpleSegmenter::segmentMultiBand` with multi-spectral band smoothing and composite tuple quantization.
- [04 — OBIA GUI Export Seams & Workflow Session Integration](issues/04-obia-gui-export-and-workflow-session-bridge.md) — Forwarded `threshold` configuration parameter and verified vector polygon export.

## Not yet specified

- **Cloud-native Vector Export & Polygon Topology**: 批量导出超大矢量分割块（GPKG/GeoJSON）及拓扑化平滑逻辑。
- **Deep Learning Instance Segmentation Bridge**: 接入 SAM (Segment Anything Model) 或 YOLOv8-seg 外部 ONNX 模型作为对象分割前置算子。

## Out of scope

- 交互式手绘/像素级画笔手工修边工具（已由 ROI 标注及交互式多点采样处理）。
- GPU 硬件加速的 3D 点云面向对象分割（仅支持 2D/多光谱 GeoTIFF 栅格）。
