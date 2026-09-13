<!-- 由 scripts/capability_knowledge_tool gen-pages 自动生成 — 手动编辑是缺陷（ADR 0146）。 -->

# RS 算子能力知识索引

覆盖 111 个 `rs:` 算子（要求 111/111）。逐算子元数据见 `data/processing/algorithm_meta/capability/`；关系图见同目录 `capability_relations.json`。

| 算子族 | 数量 | 页面 |
|---|---|---|
| 光学预处理（optical） | 17 | [capability-optical.md](capability-optical.md) |
| 光谱指数与波段运算（spectral） | 12 | [capability-spectral.md](capability-spectral.md) |
| 雷达 SAR 处理（sar） | 12 | [capability-sar.md](capability-sar.md) |
| 地形分析（terrain） | 2 | [capability-terrain.md](capability-terrain.md) |
| 时序分析（temporal） | 14 | [capability-temporal.md](capability-temporal.md) |
| 分类与机器学习（classification） | 9 | [capability-classification.md](capability-classification.md) |
| 变化检测（change） | 11 | [capability-change.md](capability-change.md) |
| 面向对象影像分析（obia） | 7 | [capability-obia.md](capability-obia.md) |
| 高光谱分析（hyperspectral） | 8 | [capability-hyperspectral.md](capability-hyperspectral.md) |
| 栅格空间分析（raster_spatial） | 15 | [capability-raster_spatial.md](capability-raster_spatial.md) |
| 数据导入（io） | 4 | [capability-io.md](capability-io.md) |

## 查询 API（D9 消费，保持稳定）

`byFamily` / `byInputModality` / `chainFrom` / `requiresGrid` / `determinismOf` / `harness:compose_chain`；清单页硬预算 64 KiB，失败模式目录 8 KiB。

## 需注意可复现性的算子

容差级（tolerance，ADR 0124）或随机性算子：

- rs:feature_normalize（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
- rs:sar_change（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
- rs:sar_speckle（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
- rs:sar_texture（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
- rs:temporal_decompose（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
- rs:temporal_smooth（容差级（并行执行与串行结果在 1e-6 相对容差内一致））
