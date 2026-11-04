# R26-science GOAL — 现有遥感科学算法、分类与大影像处理链深化

任务 ID: science | 分支: agent/r26-science-20261030T151203Z | 基线: origin/master=2610cb4741e69a89124921e44c03e340e2522ee2

## 独占写区（OWNERSHIP 摘要，详见 OWNERSHIP.md）
- src/analysis/classification/**（全目录）
- src/analysis/segmentation/**（全目录）
- src/processing/algorithms/ 中未被 #1728 占用且逐文件批准者：
  classification_postprocess.*、confusion_matrix.*、change_detection.*、
  breakpoint_detection.*、mosaic_quality.*、image_enhancement_streaming.*
- 对应新增测试（tests/，append-only 块）、docs/ 算法文档
- src/operators/rs/ 仅允许不在任何开放 PR changed-files 的算子，逐文件批准

## 只读（默认禁改）
- #1728: 10 个 rs_* 算子 + sar_metadata + spectral_classification + terrain_flow + gdal_cell_geometry
- #1726: src/operators/runtime/inference_plan.*、src/data/import_preflight.*、src/experiment/replay_deviation.*、src/geospatial/products/product_registry.*
- src/geospatial/、src/data/ 等跨轨层
- tests/surface_diff_snapshot.json（#1727+#1728 双占，本轨不改）

## 完成门（Oracle）
1. 每个工作包 DONE_TESTED（RED/GREEN×2 证据）或 BLOCKED_NOT_DONE/SUPERSEDED
2. 关键构建+定向 ctest 两轮绿；注册完整性检查通过
3. R1/R2 独立评审 P0/P1=0
4. gh pr create 成功，不自动合并
