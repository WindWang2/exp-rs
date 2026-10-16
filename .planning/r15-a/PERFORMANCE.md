# R15-A PERFORMANCE — 性能影响评估

> 范围：`origin/master(46791bc6)...HEAD` 全部生产改动的性能面。
> 结论先行：**无热路径改动，无可测量性能影响。**

## 改动逐项分析

| 改动 | 执行频次 | 开销 | 结论 |
|---|---|---|---|
| A03-6 守卫：`panDataset.hasGeoTransform() != msDataset.hasGeoTransform()` | 每次 processNativeFusion 调用 1 次 | 2× O(1) GDAL 元数据读（GDALGetGeoTransform 返回码判定） | 纳秒级；相对融合本身的栅格 I/O 可忽略 |
| A03-6 日志修复：`%.6g` → `arg(x, 0, 'g', 6)` | 仅异构分辨率融合时 1 次/调用 | 4 次 double→string 格式化 | 日志级；旧代码同样格式化（且告警） |
| A09 谓词：epsilon（abs+cmp）→ 契约（isfinite+eq） | 每像素每波段 1 次（仅内存 segment() 重载；文件入口路径先洗 NaN） | 严格减少：abs 减法 + 比较 → 单次相等比较 | 中性偏好 |
| O2 metadata 文档 | 无（operator 元数据查询） | — | 零 |

## 热路径未触碰证明

- 流式融合瓦片循环（image_fusion.cpp tile loops）、brovey/IHS/linear 核、readMsWindow/readPanWindow 的洗涤逻辑、`readBandWindow`/`readBandWindowScaled`（gdal_dataset_wrapper.cpp，**未修改**）——均无改动。
- A09 谓词位于像素有效性判定（每像素 O(bands) 的既有循环内），仅替换比较表达式，不改变循环结构或内存访问模式。

## 测试墙钟时间（证据，非基准）

| 套件 | 墙钟 | 用例/断言 |
|---|---|---|
| test_image_fusion | 22.5 s | 36 / 1615 |
| test_spectral_classification | 18.7 s | 30 / 197 |
| test_obia_segmentation | 未单独计时（< 60 s） | 14 / 850 |

（含 GDAL/Qt 初始化与 36 个 fixture 的 GTiff/VRT 创建开销；本轨无 perf 门禁目标改动。）

## 结论

无需性能回归测试；无需基准对比。改动均为 O(1) 每调用或中性偏好。
