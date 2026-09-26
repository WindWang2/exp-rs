# HELP_CONSISTENCY — commands.json 76/76 对照（WP-F）

口径：结构完备性（purpose/keywords/related 按 help_content_store 合成层校验为准，本表为静态复核）
+ 语义一致性（command.rs.* 的 purpose ↔ 算子 capability summary 抽查 18 条，零矛盾）。

- 条目总数：76（与实测 76 一致）；静态复核问题条目：19（见备注列；最终判定以 tests/test_help_integrity_12 的合成校验为准）
- 算子↔帮助完备性由 operator_help_provider 从 schema 自动派生（base tier 覆盖全部注册算子），无手工对照缺口可产生。

| # | id | purpose 非空 | keywords | related 全可解析 | 备注 |
|---|---|---|---|---|---|
| 1 | command.project.new | Y | Y | Y | — |
| 2 | command.project.open | Y | Y | Y | — |
| 3 | command.project.save | Y | Y | Y | — |
| 4 | command.project.saveAs | Y | Y | Y | — |
| 5 | command.project.importLayer | Y | Y | Y | — |
| 6 | command.project.stacBrowse | Y | Y | Y | — |
| 7 | command.project.newLayout | Y | Y | Y | — |
| 8 | command.project.exit | Y | Y | Y | — |
| 9 | command.layer.addRaster | Y | Y | Y | — |
| 10 | command.layer.addVector | Y | Y | Y | — |
| 11 | command.layer.properties | Y | Y | Y | — |
| 12 | command.layer.remove | Y | Y | Y | — |
| 13 | command.layer.zoomTo | Y | Y | Y | — |
| 14 | command.layer.toggleEditing | Y | Y | Y | — |
| 15 | command.layer.saveEdits | Y | Y | Y | — |
| 16 | command.layer.newVector | Y | Y | Y | — |
| 17 | command.layer.attributeTable | Y | Y | Y | — |
| 18 | command.map.zoomIn | Y | Y | Y | — |
| 19 | command.map.zoomOut | Y | Y | Y | — |
| 20 | command.map.zoomFull | Y | Y | Y | — |
| 21 | command.map.pan | Y | Y | Y | — |
| 22 | command.map.identify | Y | Y | Y | — |
| 23 | command.map.measureDistance | Y | Y | Y | — |
| 24 | command.map.measureArea | Y | Y | Y | — |
| 25 | command.map.refresh | Y | Y | Y | — |
| 26 | command.map.compareLayers | Y | Y | Y | — |
| 27 | command.map.swipe | Y | Y | Y | — |
| 28 | command.workbench.classify | Y | Y | N | 未解析 related: ['operator.rs.supervised_classification'] |
| 29 | command.workbench.georefI2I | Y | Y | Y | — |
| 30 | command.workbench.georefI2M | Y | Y | Y | — |
| 31 | command.workbench.obia | Y | Y | N | 未解析 related: ['operator.rs.obia_segment'] |
| 32 | command.rs.bandMath | Y | Y | N | 未解析 related: ['operator.rs.band_math', 'operator.rs.spectral_index'] |
| 33 | command.rs.spectralIndex | Y | Y | N | 未解析 related: ['operator.rs.spectral_index'] |
| 34 | command.rs.contrastStretch | Y | Y | N | 未解析 related: ['operator.rs.contrast_stretch'] |
| 35 | command.rs.spatialFilter | Y | Y | N | 未解析 related: ['operator.rs.image_enhancement'] |
| 36 | command.rs.pca | Y | Y | N | 未解析 related: ['operator.rs.pca', 'operator.rs.mnf'] |
| 37 | command.rs.bandRatio | Y | Y | N | 未解析 related: ['operator.rs.band_ratio'] |
| 38 | command.rs.mosaic | Y | Y | N | 未解析 related: ['operator.rs.mosaic'] |
| 39 | command.rs.changeDetection | Y | Y | N | 未解析 related: ['operator.rs.change_detection'] |
| 40 | command.rs.atmospheric | Y | Y | N | 未解析 related: ['operator.rs.atmospheric_dos1', 'operator.rs.atmospheric_dos2', 'operator.rs.atmospheric_quac'] |
| 41 | command.rs.qaMask | Y | Y | N | 未解析 related: ['operator.rs.qa_mask'] |
| 42 | command.rs.applyMask | Y | Y | N | 未解析 related: ['operator.rs.apply_mask'] |
| 43 | command.rs.radiometric | Y | Y | N | 未解析 related: ['operator.rs.radiometric_calibration', 'operator.rs.dn_to_radiance'] |
| 44 | command.rs.ortho | Y | Y | Y | — |
| 45 | command.rs.terrain | Y | Y | N | 未解析 related: ['operator.rs.terrain_analysis'] |
| 46 | command.rs.fusion | Y | Y | N | 未解析 related: ['operator.rs.image_fusion'] |
| 47 | command.rs.temporal | Y | Y | N | 未解析 related: ['operator.rs.temporal_composite'] |
| 48 | command.rs.speckle | Y | Y | N | 未解析 related: ['operator.rs.sar_speckle'] |
| 49 | command.rs.extractBands | Y | Y | N | 未解析 related: ['operator.rs.extract_bands'] |
| 50 | command.workbench.processingHistory | Y | Y | Y | — |
| 51 | command.workbench.temporal | Y | Y | Y | — |
| 52 | command.workbench.datasetExperiment | Y | Y | Y | — |
| 53 | command.workbench.model | Y | Y | Y | — |
| 54 | command.app.commandPalette | Y | Y | Y | — |
| 55 | command.workflow.new | Y | Y | Y | — |
| 56 | command.workflow.open | Y | Y | Y | — |
| 57 | command.workflow.save | Y | Y | Y | — |
| 58 | command.workflow.run | Y | Y | Y | — |
| 59 | command.workflow.stop | Y | Y | Y | — |
| 60 | command.view.linkCenter | Y | Y | Y | — |
| 61 | command.view.linkScale | Y | Y | Y | — |
| 62 | command.view.linkCursor | Y | Y | Y | — |
| 63 | command.view.linkVisibility | Y | Y | Y | — |
| 64 | command.view.linkUndo | Y | Y | Y | — |
| 65 | command.view.linkGroupStatus | Y | Y | Y | — |
| 66 | command.view.linkUnlinkAll | Y | Y | Y | — |
| 67 | command.workbench.visualAnalytics | Y | Y | Y | — |
| 68 | command.workbench.operatorCatalog | Y | Y | Y | — |
| 69 | command.workbench.cartography | Y | Y | Y | — |
| 70 | command.workbench.classifyStudio | Y | Y | Y | — |
| 71 | command.workbench.georefDual | Y | Y | Y | — |
| 72 | command.workbench.ir2Pipeline | Y | Y | Y | — |
| 73 | command.cartography.compose | Y | Y | Y | — |
| 74 | command.cartography.preflight | Y | Y | Y | — |
| 75 | command.cartography.repair | Y | Y | Y | — |
| 76 | command.cartography.export | Y | Y | Y | — |
