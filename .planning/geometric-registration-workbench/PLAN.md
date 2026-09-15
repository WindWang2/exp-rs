# PLAN — D14 智能几何校正、精细图像配准与多视口比对工作台

> 规格: `~/DEVON/14_geometric_registration_workbench.md`（D14, 9 工作包 A–I）
> 方法: Matt Pocock TDD 纵向切片（Tracer Bullet → 核心数学 → 边界鲁棒），单切片原子提交。

## 公共测试接缝总览（Pre-agreed Public Seams）

| 包 | 头文件 | 命名空间 | 测试目标 | 真值来源（防同义反复） |
|---|---|---|---|---|
| A | `src/processing/algorithms/gcp_manager.h` | `rs::core` | `test_gcp_manager` | 100×100 网格 5 点凸包=10000、覆盖率=1、残差 0.3/0.4→RMSE=0.5（鞋带公式/勾股手算） |
| B | `src/processing/algorithms/geometric_transform.h` | `rs::algorithms` | `test_geometric_transform` | θ=30°、s=1.5、t=(25,-10) 解析系数（cos30=√3/2） |
| C | `src/processing/algorithms/tps_interpolator.h` | `rs::algorithms` | `test_tps_interpolator` | 5 节点中心扰动 (50,50)→(55,52) 精确插值残差=0；U(r) 极限 |
| D | `src/processing/algorithms/feature_matcher.h` | `rs::algorithms` | `test_feature_matcher` | 已知 H_true 单应 40 内点 + 20 注入粗差 → 100% 剔除、保留≥38 |
| E | `src/processing/algorithms/resampler.h` | `rs::algorithms` | `test_resampler` | 核单位分解 ΣW≡1（1e-12）；解析平面 f=2x+3y+10 在 (1.4,2.6)=20.6 |
| F | `src/processing/algorithms/pansharpening.h` | `rs::algorithms` | `test_pansharpening` | 合成反射率场；Wald 协议 ERGAS≤2.5、CC≥0.94、均值漂移<1% |
| G | `src/app/workbench/georef_dual_window.h` | `rs::app` | `test_georef_dual_window` | 无头 QApplication；防重入递归深度=1；表格行数/RMSE 联动 |
| H | `src/agent/tools/geometric_tool.h` | `rs::agent` | `test_geometric_agent_tools` | 8 点 JSON 第 4 点 15 像元粗差 → 3σ 准则必标 GCP_04 |
| I | `tests/test_d14_geometric_registration_e2e.cpp` | — | `test_d14_geometric_registration_e2e` | 生产链路实时判分：lab06=100、lab07=100、劣质输入精准扣 35 |

## 里程碑

- **M0（Phase 0）**: worktree ✓ / BASELINE ✓ / DECISIONS ✓ / ADR-0159 / 本 PLAN / .gitignore 白名单 / configure ✓ → 首次原子提交。
- **M1（Pkg A+B）**: A 三切片（CRUD 示踪弹 → 凸包+Clark-Evans → Delaunay+残差 RMSE+CSV/JSON）；B 三切片（Translation 示踪弹 → SVD 仿射+Helmert+相似 → Hartley P2/P3+DLT+病态拦截）。每切片 Red→Green→原子提交。
- **M2（Pkg C+D）**: C 三切片（U(r) 基函数 → 增广系统 5 点精确插值 → λ 正则+弯曲能量+批量）；D 三切片（结构体+比例检验骨架 → RANSAC 4 点 DLT+内点投票 → 重投影 RMSE+图像缓冲对接）。
- **M3（Pkg E+F）**: E 三切片（1D 核 → 2D 邻域插值 → warpRaster 逆向映射+NoData 防渗）；F 三切片（Brovey → GS 正交投影+直方图匹配 → Wald ERGAS/CC/SSIM 评估）。
- **M4（Pkg G）**: G 三切片（无头布局+QPointer → 防重入同步+刺点表格 → 残差箭头+卷帘开关）。
- **M5（Pkg H+I）**: H 三切片（schema → 3σ 粗差+模型推荐决策树 → 对齐前检+信封封装）；I 三切片（脚手架 → 全链路 → lab06/lab07 判分正反用例）。
- **M6（Phase 3 审查）**: ≤3 只读 subagent 双轴审查（Standards: C++20/Qt6 QPointer/防重入；Spec: 变换保真/能量守恒/ERGAS≤2.5/CC≥0.94）→ `REVIEW_LOG.md` P0=P1=0。
- **M7（收官）**: 全量 D14 ctest 绿灯 → `EVIDENCE.md` 归档 → push → PR。

## 接线清单（CMake / 基础设施）
1. `src/processing/CMakeLists.txt`: `add_library(sicnu_processing SHARED ...)` 追加 gcp_manager / geometric_transform / tps_interpolator / feature_matcher / resampler / pansharpening 的 .cpp。
2. `src/agent/CMakeLists.txt`: `add_library(sicnu_agent SHARED ...)` 追加 tools/geometric_tool.cpp。
3. `src/app/CMakeLists.txt`: app 目标追加 workbench/georef_dual_window.{h,cpp}（含 Qt moc 自动化，qt_add_executable）。
4. `tests/CMakeLists.txt`: A–F、H、I 用 `sicnu_add_test(...)`（已链接 sicnu_processing/sicnu_agent/Qt6/qgis）；G 用自定义 `add_executable` 编入 `src/app/workbench/georef_dual_window.cpp`（仿 test_accuracy_panel_wiring）。
5. `.gitignore`: 白名单 `!.planning/geometric-registration-workbench/`。

## 切片纪律（反模式红线）
- 禁止横向切片：每切片 = 接缝骨架 + 失败测试 + 最小实现 + 提交，完整闭环后再进下一片。
- 禁止同义反复：期望值只来自解析解/标准（见上表"真值来源"列），绝不以被测代码反算。
- 禁止实现耦合：只断言 Public Seam 可观测行为；无 `#define private public`、无友元注入、无内部 Mock。
- 重构后置：YAGNI，全绿后再进 M6 审查重构。
