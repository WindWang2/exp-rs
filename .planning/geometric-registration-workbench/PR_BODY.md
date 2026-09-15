# D14 · 智能几何校正、精细图像配准与多视口比对工作台

ADR: `docs/adr/0159-geometric-registration-workbench.md` · 规划: `.planning/geometric-registration-workbench/`

## 内容

9 个工作包（A–I），全部为新增独立模块，零第三方新依赖：

| 包 | 模块 | 要点 |
|---|---|---|
| A | `sicnu_processing` `rs::core::GcpManager` | GCP CRUD + Andrew 凸包/鞋带面积 + Clark-Evans 最近邻指数 + Bowyer-Watson Delaunay 纵横比 + 残差/RMSE + CSV/JSON 往返 |
| B | `rs::algorithms::GeometricTransform` | 7 种模型闭式解：Translation/Rigid/Similarity（Procrustes）、Affine/P2/P3（Hartley 归一化正规方程 + 单边 Jacobi SVD 伪逆 + 系数实坐标还原）、Projective（归一化 DLT）；κ>1e14 与秩亏安全失败 |
| C | `TpsInterpolator` | U(r)=r²ln r、(N+3) 增广系统部分主元 LU、λ 正则化、弯曲能 WᵀKW/(16π)、精确仿射再现 |
| D | `FeatureMatcher` | Lowe 比例检验 + 确定性 RANSAC（mt19937(42)、4 点 DLT、自适应迭代、内点重拟合）+ 简化梯度直方图/强度斑描述符的图像级入口 |
| E | `Resampler` | 最近邻/双线性/Keys 三次/Lanczos-3、核单位分解 1e-12、NoData 50% 有效权重防渗、clampRange 过冲截断、逆向映射 warp |
| F | `PanSharpening` | Gram-Schmidt（Aiazzi/Laben 投影替代）、Brovey、IHS（G&W 圆形色度模型，精确互逆）、HPF；ERGAS/CC/RMSE/SSIM Wald 协议指标 |
| G | `rs::app::GeorefDualWindow` | 双 QgsMapCanvas 联动（QPointer + mApplyingSync 防重入 + 16ms 节流）、GCP 表格实时重解算、残差矢量箭头、卷帘对比（复用 SwipeMapTool） |
| H | `rs::agent::GeometricTool` | `spatial:geometric_registration`：3σ 粗差审计（含禁用后预期 RMSE）、模型推荐决策树、GDAL 特征对齐前检；JSON Schema draft-07；结构化信封，异常零穿越 |
| I | `test_d14_geometric_registration_e2e` | GCP→P2 拟合→三次卷积 warp→GS 锐化→Wald 评估全链路；lab06=100、lab07=100、劣化输入精准扣 35（RMSE=2.5 由 δ=40/√39 解析构造）；判分全部由生产指标实时计算 |

## 验证

- 9 个新测试目标 65 用例 100% 绿（`QT_QPA_PLATFORM=offscreen`、ctest -j1、零远端 CI）。
- 断言真值全部独立于实现：鞋带/勾股手算、θ=30°+s=1.5 解析系数、核单位分解 1e-12、解析平面 20.6、H_true 单应、CE 闭式 √10/4.0、Wald 物理门（ERGAS≤2.5、CC≥0.94）。
- 黑盒纪律：无私有探测、无内部 Mock、无同义反复（TEST_MATRIX.md 逐条列真值来源）。
- 全量 `ninja -j2` 构建 + runbook ctest 正则回归（详见 EVIDENCE.md）。

## 有意偏离（DECISIONS.md 留痕）

- 规格书 `src/core/` 是 QGIS vendor 树 → GcpManager 落位 `src/processing/algorithms/`，命名空间保持 `rs::core`。
- lab06/lab07 教学 JSON 由 labspec schema 锁定且非本 track 领地 → 不改动；判分规则在 e2e 内实时计算。
- `test_georef_dual_window` 名称已被既有 QGIS georeferencer 测试占用 → 新测试命名 `test_georef_dual_window_workbench`（仍匹配 runbook 正则）。
