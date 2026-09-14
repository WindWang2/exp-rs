# DECISIONS — D14 Geometric Registration Workbench

> 工程决策分歧记录（autonomy=full，无人值守模式下直接采用最佳实践并在此留痕）。

## D1 · 文件落位偏离规格字面路径（`src/core/` 不可用）
规格书写 `src/core/gcp_manager.{h,cpp}`。实地勘察确认 **`src/core/` 是 vendor 的 QGIS 源码树**（`qgis_core` 目标，`qgsapplication.h`、`pal/`、`labeling/` 等），向其注入 `rs::` 命名空间文件会污染上游 vendor 目录且无 CMake 接线。
**决策**: GcpManager 落位 `src/processing/algorithms/gcp_manager.{h,cpp}`，编入 `sicnu_processing`（SHARED，被 sicnu_agent/app/tests 链接）。命名空间仍按规格 `rs::core`——仓库已有 `rs::algorithms`（band_math_simd.h:108）、`rs::display`（src/app/display）先例，家族一致。

## D2 · 命名空间沿用规格（`rs::core` / `rs::algorithms` / `rs::app` / `rs::agent`）
仓库混用 `sicnu::` 与 `rs::` 两族。D14 为全新隔离模块，规格头文件签名明确 `rs::*`；沿用规格可保证与 D14 规格书测试伪代码零偏差。已确认无同名冲突。

## D3 · SVD 求解器选型：单边 Jacobi（one-sided Jacobi），零外部依赖
- **候选**: ① 引入 Eigen（vcpkg 已有？否——仓库未依赖 Eigen，新增重量级依赖违逆 YAGNI 与 Surgical Changes）；② `A^T A` 特征分解（法平方条件数 κ 翻倍，规格明确要求数值稳定）；③ **单边 Jacobi SVD**（Golub–Kahan 精度级，逐列正交化，实现 ~120 行，对小稠密矩阵（≤60×20）收敛快且数值最稳）。
- **决策**: ③。奇异值截断阈值 σ_i < 10⁻¹²·σ_max → 置零（规格 1e-12 契约）；伪逆 Σ⁺ 用 1/σ_i。
- 条件数 κ(A) = σ_max/σ_min；σ_min=0 时报告 κ=inf（序列化为 `std::numeric_limits<double>::infinity()`，`success=false` 当 κ>1e14）。

## D4 · 多项式（P2/P3）正规方程 + Hartley 归一化
高阶幂基直接装配法方程条件数爆炸（规格明确）。**决策**: 求解前对 (u,v)/(x,y) 各自做 Hartley 归一化（质心平移至原点、平均距缩放至 √2），在归一化空间装配正规方程并用 D3 的 SVD 伪逆求解，最后仿射逆代换还原真实系数。forwardCoeffs/backwardCoeffs 始终为**真实坐标系**系数（对外契约），conditionNumber 报告归一化空间的 κ。

## D5 · DLT 单应：直接线性化 + SVD 最小奇异向量
2N×9 矩阵 M，h = 最小奇异值对应右奇异向量（‖h‖=1），h₃₃ 归一化存储。逆向系数通过 H⁻¹（伴随矩阵法，3×3 闭式）获得。分母 |h₃₁x+h₃₂y+h₃₃| < 1e-12 时 `applyBackward/applyForward` 返回 (±inf, ±inf) 哨兵而不抛异常（由 warp 的范围裁剪拦截）。

## D6 · TPS 求解：部分主元 LU（增广 (N+3) 系统规模小）
Bunch-Kaufman 实现复杂度高；增广系统 N≤数百，**部分主元 Doolittle LU**（O(N³)）稳健足够。λ=0 精确插值为默认；λ>0 时 K+λI 正则化。重合点（间距<1e-9）在 fit() 前置去重。径向基 U(r)=r²ln r，r≤1e-12 短路返回 0（防 ln 0 → NaN，规格 Review 清单硬性项）。弯曲能量 I = WᵀKW/(16π)。

## D7 · RANSAC：确定性种子 + 对称传递误差
`std::mt19937(42)` 固定种子（规格 Review 硬性项，测试可重现）。采样 4 点用 DLT 闭式解；误差用**前向重投影**（规格公式）；自适应迭代 K = ln(1-p)/ln(1-(1-η)⁴)，上限 maxIters；结束后全体内点 SVD 重拟合 H。候选 <4 对或共线 → 返回单位阵 + 全 false 掩码（契约）。

## D8 · 重采样核：Keys a=-0.5、Lanczos-3、NoData 50% 有效权重规则
- 三次卷积 W(x) 三段式（规格公式），单位分解 Σ W ≡ 1（测试容差 1e-12）。
- Lanczos-3：sinc·sinc(x/3)，|x|≥3 截断。
- NoData：双线性/三次/Lanczos 邻域内有效权重占比 <50% → 输出 NoData；否则按有效权重和归一化重赋（严禁哨兵值渗入算术）。
- `clampRange=true` 时输出硬截断 [minValue, maxValue]（抑制三次卷积过冲）。
- `warpRaster` 纯逆向映射逐扫描线，输出像素级访存 `(j*width+i)` 单次乘加；无中间全幅缓冲（内存边界 = 输入幅 + 输出幅，见 ADR-0159 §5）。

## D9 · Gram-Schmidt 锐化：样本协方差 + 直方图匹配 + ε 防零
- 合成 Pan：w_i 等权缺省（bandWeights 可覆盖）。
- GS 正交化用**逐波段减投影**闭式（协方差/方差），Var < ε=1e-7 时以 ε 替代（规格防零项）。
- PAN 直方图匹配到 GS₁（μ/σ 对齐），逆投影恢复锐化波段。
- Wald 评估：ERGAS = 100·(h/l)·√(mean((RMSE_k/μ_k)²))；CC 为皮尔逊相关；SSIM 用全局均值/方差/协方差形式（7×1 常数权重简化，文档声明）。均值漂移守恒与无负值由测试断言。

## D10 · 双视窗 UI：复用仓库既有 16ms 节流 + 防重入范式
`src/app/shell/rs_dual_viewport_sync_controller.h` 已示范 `QPointer` + `mApplying` + `QTimer` 16ms 节流。GeorefDualWindow 按同一范式实现 `mApplyingSync` 守卫（规格点名）。画布层接 `QgsMapCanvas`（qgis_gui），测试通过自定义 add_executable 将 .cpp 编入测试目标（仿 test_image_warper / test_accuracy_panel_wiring 模式），`isApplyingSync()` 作为防重入探针接缝。

## D11 · lab06/lab07 JSON 不改动；判分在 e2e 内实时计算
`data/labs/lab06_georeferencing.lab.json` / `lab07_image_fusion.lab.json` **已存在**，受 `labspec.schema.json` 与 lab-content track 管辖（非 D14 可破坏面）。`data/labs/grading/lab_rules.schema.json` 的断言 kind 枚举固定（range/mean_sigma/.../crs_grid），无 GCP/RMSE/CC/ERGAS 类别，扩展该引擎属其他 track 领地。
**决策**: D14 的 100 分制判分逻辑（实验 06：GCP 数 10 + 覆盖率 20 + Clark-Evans 10 + RMSE 40 + 重采样完好 20；实验 07：分辨率 20 + CC 40 + ERGAS 40）作为 e2e 测试内的纯函数实时计算——输入全部来自生产类（GcpDistributionMetrics / PanSharpenMetrics / Resampler 输出），绝不硬编码分数（规格 Package I 第 5 条）。

## D12 · Agent 工具注册范围
`src/agent/tools/geometric_tool.{h,cpp}` 新目录编入 `sicnu_agent`。JSON 键蛇形命名（规格 Standards 轴），execute() 返回 `{success, action, data, diagnostic_message}` 信封；**绝不抛异常穿越 execute 边界**（规格 Spec 轴）。工具暂不挂入 SpatialToolRegistry 目录（目录清单归 agent 工具目录 track 管辖），仅暴露类级接缝 + 测试覆盖；如需注册再追加一行 provider 接线。

## D13 · 资源红线执行
`ninja -j2` 固定；每轮构建前 `free -g` + `uptime` 采样，RSS>70%（≈43GB）或 load>24 即降级 `-j1`；ctest 恒 `-j1`；`QT_QPA_PLATFORM=offscreen`。
