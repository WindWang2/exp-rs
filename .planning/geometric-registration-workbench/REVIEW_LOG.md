# REVIEW_LOG — D14 Geometric Registration Workbench

Phase 3 双轴只读审查（≤3 subagents 红线内实际派发 2 个，未递归派生）。
审查基线：9 包全部实现 + 65 用例绿灯后的 HEAD。
结论：**P0 = 1 → 已修复；P1 = 5 → 全部修复；P2 = 采纳 11 项 / 记录 2 项 / 暂缓 5 项**。
修复后 D14 套件重跑 **65/65 全绿**。

## Standards 轴

### P0（已修复）
| # | 位置 | 问题 | 处置 |
|---|---|---|---|
| S-P0-1 | georef_dual_window.cpp onAddGcpPoint | GCP id 用 `size()+1`，删除任一点后撞号 → addPoint 永久静默失败 | 改单调序号 `mNextGcpSerial` + 唯一性探测 |

### P1（已修复）
| # | 位置 | 问题 | 处置 |
|---|---|---|---|
| S-P1-2 | loadSource/ReferenceImage | QgsRasterLayer 无主、重复加载泄漏 | `setParent(this)` 挂窗口对象树 + 替换时 deleteLater 旧层 |
| S-P1-3 | onExecuteWarpClicked | 注释称真实 warp 但从不写文件，发出不存在路径 | 诚实化：恒发空路径 + 注释说明导出归 session/任务管线（DECISIONS D14-15） |
| S-P1-4 | geometric_tool.cpp inspectMisalignment | agent 可传任意大栅格 → OOM | GDAL 读取期降采样（长边 ≤512px） |

### P2（采纳 8 项）
- TPS 头注释与实现不符 → 修正注释（两次因式分解，规模小代价可忽略）。
- sampleWeighted 热循环堆分配 → `std::array<double,6>` 栈权重。
- gradientMagnitudeAt 挂错注释 → 移除（简化描述子决策补记 DECISIONS D14-15）。
- Projective applyForward/Backward 无系数长度守卫 → `c.size() >= 9` 守卫（不足恒等返回）。
- mDefaultTool 死代码 → 删除。
- `mouse->pos()` Qt6 弃用 → `event->position().toPoint()`。
- 测试条件断言 `if (res2.success)` → 无条件 REQUIRE。
- Rigid 源点全重合 t=0 静默成功（Spec 轴交叉发现）→ 统一早退失败。

### P2（暂缓，记录在案）
- 顶点去重逻辑两处复制（gcp_manager）——待后续重构窗口。
- matchImages 裸指针接口与 span 风格不统一——接口稳定后统一。

## Spec 轴

### P1（已修复）
| # | 位置 | 问题 | 处置 |
|---|---|---|---|
| C-P1-1 | gcp_manager triangleAspectRatio | 退化三角形纵横比返回 0（=最优），与"诊断共线应趋∞"方向相反 | 退化 → +∞；全共线（空三角网）保持 0.0，由凸包面积 0 语义覆盖（DECISIONS D14-15） |
| C-P1-2 | linalg dltHomography | σ_min 跳过截断值导致退化配置 κ 正常、绕过门禁 | 秩门禁：非零奇异值 <8（齐次零空间+最小补零行属固有）判失败；κ=σmax/σ8。修正过程中验证"精确数据 DLT 秩恒 ≤8"，避免了中间版"要求秩 9"的错误判据 |

### P2（采纳 5 项）
- Lowe 检验 secondDist==0（描述符完全重复）反被保留 → 显式拒绝。
- loadFromCsv 非法数字静默变 0.0，违背"Malformed rows are skipped"契约 → ok 标志校验。
- RANSAC/描述符简化、inverseCoordMap 世界坐标语义 → 补记 DECISIONS D14-15。
- D3 决策文字与秩亏从严实现漂移 → D3 修订对齐。
- inspect 平移容差与 8px 采样网格量化余量对齐（2.0→4.0，含注释）。

## 修复过程中发现并修复的额外缺陷（非审查直报）
- DLT 秩门禁首版误设"n≥5 需秩 9"——精确对应数据的 DLT 矩阵本征秩 ≤8（真值 h 在零空间中），
  25 点单应测试即刻揭穿；修正为统一 requiredNonZero=8。教训：齐次系统的"满秩"语义与
  最小二乘系统不同，门禁必须按零空间维数推导。
- 近共线 Affine 测试用平移真值构造——平移保持共线，逆向解算必然秩亏失败（行为正确，
  数据不当）；改用 1° 旋转真值。

## 审查通过项（两轴汇总）
- 单边 Jacobi SVD 伪逆（含 σ² 双除约定）、DLT 最小奇异向量、Hartley 归一化——独立验算正确。
- Procrustes 刚性/相似闭式解、Helmert 自由度划分正确（规格枚举注释自相矛盾处按头文件布局自洽）。
- TPS 增广系统装配、λ 位置、弯曲能 16π、ln0 短路；RANSAC 确定性种子与自适应迭代公式；
  Clark-Evans 闭式；Keys a=-0.5 / Lanczos-3 / NoData 50% / clampRange；ERGAS h/l 方向；
  3σ 粗差准则与模型推荐树阈值；e2e 负向 δ=40/√39 ⇒ RMSE=2.5 精确推导——全部通过。
- Qt6：QPointer 观测、mApplyingSync 递归深度恰 1、QTimer 生命周期；析构内 delete SwipeMapTool
  经核验为正确时序（工具父对象为画布，其析构需先于 scene 销毁删除 CanvasItem）。
- 测试黑盒纪律：无私有探测/友元/内部 Mock；期望值全部解析独立。
