# R18-A CROSS_TRACK_FINDINGS — 跨轨发现（只记录，不越界修）

## CF-1 #1516 合并静默回滚 #1514（机制性风险，影响所有轨道）
- 事实：PR #1516（r15-D）的分支在其基点后合并过旧 master；#1514（r15-A）15:59 落地后，
  #1516 16:12 合并时 `src/processing/algorithms/image_fusion.cpp` 的冲突解决取了分支侧
  blob（d2f9e3275），静默丢弃 master 侧防护（349463c91）。共 678 行（含 3 个测试文件）。
- 本轨动作：WP1 恢复（本轨写区内）。
- 跨轨建议：任何轨道合并前应 `git diff <merge-base>..origin/master -- <own files>` 复查
  是否有他轨新改动被自己的分支侧覆盖；建议在 CI/review 检查表加一条
  “merge 后 `git diff HEAD^1 HEAD -- .` 中出现的纯删除需逐块说明来源”。
  （本轨不改 CI 工作流 —— 只记录。）

## CF-2 #1467 剩余两项（前人已核验搁置）
- `src/processing/CMakeLists.txt` spatial_split 迁出：Windows `WINDOWS_EXPORT_ALL_SYMBOLS`
  下链接失败（58 个 `rs::core::*` 模板实例化出现在导出集），前人已撤销分支
  `hardening/r17-H-spatial-split-ownership`。需要构建系统域 owner 解析。
- `rs_spatial_cross_validation.cpp` bufferedBlockFolds 与 spatial_split 不变式：语义不等价
  （60/20/20 vs leave-one-block-out K 折），合并是设计裁决，留给 classification 域。

## CF-3 #1491 master CI 无完成结果
- 本轨不等线上 CI（任务书硬约束）；本地 Oracle 为交付依据。CI 恢复是 infra 轨道职责。

## CF-4 SAR 地形算子 DEM 网格仅查 dims（审计 A3-D2/D3）
- `rs_sar_terrain_flatten/correction` 对 DEM 只比对宽高，不比对 geotransform/CRS；
  且流内核无取消钩子。SAR 家族是否属本轨 `src/operators/rs/**` 传统算子范围？
  ——是（非模型推理），但 SAR 地形内核 `src/processing/algorithms/sar/sar_terrain.cpp`
  涉及较深几何语义，本轨列 Batch-2 候选，优先级低于融合/分类主线；若时间不足则
  仅保留此记录给后续 SAR 专项。

## CF-5 质量报告参考重采样不掩 NoData（known limitation，不修）
- `readBandWindowScaled`（gdal_dataset_wrapper.cpp:441-451）GRIORA_Bilinear 会把声明
  sentinel 混入有限参考值；#1514 已将特定 fixture 定性为误报，现行代码在
  image_fusion.cpp:869-872 有文档化说明。`src/processing/gdal/**` 属“允许小范围”区，
  但该函数被多轨共享（#1513 也可能触碰）——本轨不接管，WP1 恢复的测试已把现行为
  钉住（dual-lock），防止无声漂移。

## CF-6 .gitignore 通用规则被 #1516 分支侧覆盖丢失
- `git diff c6b96a5e2 HEAD -- .gitignore`：#1516 合并取分支侧时丢掉
  `!data/labs/`、`!data/labs/**`、`__pycache__/`、`*.pyc` 与一条重复的
  `!.planning/r14-B/`。后三条意味着 Python 缓存可能出现在 git status。
- .gitignore 为根共享文件（多轨活跃写入），本轨不越界修；建议 infra/构建域
  轨道核对该文件近 60 行演进而决定是否补回。与 CF-1 同根因（合并取侧未审）。
