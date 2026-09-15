# TEST_MATRIX — 能力 → 独立 oracle → 命令 → exit → evidence

| 能力 | 测试 target | 独立 oracle（不复用被测实现） | 命令 | exit |
|---|---|---|---|---|
| A plan: union extent/offsets | test_mosaic_plan | 手算网格几何 | ctest -R test_mosaic_plan -j1 | — |
| A plan: 混合 CRS 足迹诊断 | test_mosaic_plan | OGR 独立变换期望值 | 同上 | — |
| B 匀色: 已知 gain/offset 恢复 | test_mosaic_balancing | 合成场景已知 gain 真值 | ctest -R test_mosaic_balancing -j1 | — |
| B 匀色: 异常拒绝/无 overlap 失败 | test_mosaic_balancing | 负测试 | 同上 | — |
| C seamline: 已知最优路径 | test_mosaic_seamline | 手算成本面 DP 真值 | ctest -R test_mosaic_seamline -j1 | — |
| C seamline: tie-break 确定性 | test_mosaic_seamline | 对称输入=固定选择 | 同上 | — |
| D blend: 权重恒一/无裂缝 | test_mosaic_blend | Σw=1 + 边界值 | ctest -R test_mosaic_blend -j1 | — |
| E quality: score/provenance | test_mosaic_quality | 手算 score/已知索引 | ctest -R test_mosaic_quality -j1 | — |
| F 融合报告: Q/RASE 已知值 | test_fusion_quality_report | 手算常数图像指标 | ctest -R test_fusion_quality_report -j1 | — |
| G 原子输出/overview/sidecar | test_quality_mosaic_operator | GDAL 重开校验 | ctest -R test_quality_mosaic_operator -j1 | — |
| H 规模/内存/cancel | test_mosaic_scale | 峰值字节计数不随逻辑规模增长 | ctest -R test_mosaic_scale -j1 | — |
| E2E 多场景全链路 | test_quality_mosaic_operator | known-answer + provenance 追溯 | 同上 | — |
| 回归: 既有 mosaic/fusion 套件 | test_mosaic / test_pansharpening / test_image_fusion | 既有套件不破坏 | ctest -R "test_mosaic$|test_pansharpening|test_image_fusion" -j1 | — |
