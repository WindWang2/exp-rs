# TASKS — zcode-r20-01

## 原创发现（F-xx）：全部经主 Agent 逐行复核 + 独立审查代理交叉确认

| ID | 严重度 | 位置 | 缺陷 | 触发案例 | 失败 oracle | 状态 |
|---|---|---|---|---|---|---|
| F-01 (T1) | **P1** | trend_analysis.cpp:175-190 | computeRasterTrend 紧凑化只压缩值不压缩时间戳，幸存值与 ts 前缀错配；序列中部缺帧时 Theil-Sen 分母全错（S/z/p 因只依赖值序而侥幸不变） | 光学时序云遮挡缺中间帧 → 斜率图系统性错误 | 解析 oracle：t={0,10,20,30}, y={1,NaN,2,3} → 真值 2/30，buggy 0.1；单序列入口（正确）与批入口（错）不一致 | 修复+测试已写，待 RED/GREEN |
| F-02 (T2) | P2 | terrain_viewshed.cpp:41-57 | viewshedR3 从不写 nodataByte，头文件契约（"visible holds 1/0/nodataByte for DEM-NoData cells"）违约；agent 地形工具（terrain_spatial_tools.cpp:358 传 255）把 NoData 计入 analysed 分母 | 带 NoData collar 的 DEM → visibleFraction 系统性压低 | 含 collar DEM 断言每个 NoData 像元 ==255、analysed==168 | 同上 |
| F-03 (T3) | P2 | mosaic_plan.cpp:194-206 | 网格资格只防 Y 镜像（yDirectionMismatch），X 方向（gt[1] 符号）不查；X 翻转场景 1:1 列映射 → 内容镜像进镶嵌，静默几何损毁 | 混入 gt[1]<0 的东西翻转产品 → 镶嵌内容左右镜像 | 两幅同网格栅格、B gt[1] 取负 → 断言 xDirectionMismatch 诊断 + gridEligible=false + warning | 同上 |
| F-04 (C1) | P2 | rs_classifier_mlp.cpp:295-302 + pipeline:142/267-271 | 单类训练时 mClassLabels 填充 [c,c]，classOrder() 返回重复 id 违反"严格递增"契约；sidecar 写入→ RsClassOrder::fromJsonArray 拒载 → 自己保存的模型 predict-only 报"sidecar 损坏" | 全部 class_id=1 的 ROI 以 MLP 训练+保存 → modelIn 加载硬失败 | 断言单类 fit 后 classOrder 严格递增且 size==1，sidecar round-trip 成功 | 同上 |
| F-05 (C2) | P3 | rs_classifier_isodata.cpp:64-66 | fit 未校验 y（type/cols/rows）就读 at<int>；CV_32F 标签类型双读、(rows>0,cols==0) 空 data 解引用；KMeans 同构读取已有守卫并注释 | 外部调用方传 CV_32F/Nx2/行数不匹配 y → m_remapNeeded 基于垃圾位 | 断言 fit 返回 false（fail-closed），空 y 仍为合法哑输入 | 同上 |
| F-06 (S1) | P2 | sar_calibration.cpp:202-208+finishValue | 栅格入射角只查 isfinite，不查 (0°,90°) 物理域；θ=90° 时 cos≈6e-17 → γ⁰≈1.6e16 有限正值穿过 finishValue v>0 门写入产品（现有测试注释声称"finishValue 会钳制"——该假设为假）；兄弟模块 sar_terrain_geometry 同角域有显式拒绝 | 入射角栅格边缘 90.0/>89.999° 样本 → +162dB 毒值入产品 | θ∈{90,0,-5,120} 断言 NaN，θ∈{30,89.99} 断言解析值；常量 θ0=95 同样拒绝 | 同上 |
| F-07 (S3) | P2/P3 | sar_insar.cpp fit() | 相位斜坡正规方程用未中心化原始像素坐标：20k 场景 x⁴~1.6e17 vs O(n) 常数项，条件数 ≥1e17 超 double 分辨率；invertDenseMatrixInPlace 绝对 1e-12 阈值误判奇异或解出噪声系数 | 大场景 rs:sar_interferogram --ramp quadratic → 系数漂移/拟合失败 | 稀疏格点 [0,20000]² 已知系数曲面 → 断言系数相对误差 <1e-4；平移 100k 不变性；退化秩亏仍 fail-closed | 同上 |

## 审查代理报告但不修的项（记档）

- fast_6s_lookup.cpp:115-116 水汽吸收上/下行 0.5 不对称系数——需要权威辐射传输参考才能定性（无 6S/MODTRAN oracle），修复风险大于收益；已在 ORACLES.md 记录提案。
- image_fusion.cpp 内存核均值样本集 vs 协方差样本集不一致（流式路径正确）——内存 seam 仅 tests/panel 调用；记为后续提案。
- image_fusion 内存核 ±Inf 门只测 isnan（流式已洗）——同上 seam 级。
- rs_simple_segmenter 31-bit 哈希合并碰撞——确定性碰撞但影响需统计量化；记档。
- #1591 RsSegmentMap 潜在竞态——无活跃跨线程调用方，不引入锁开销；记档提案。

## Work Package 状态

| WP | 状态 | 说明 |
|---|---|---|
| A01 算法普查 | PARTIAL | 三路代理普查 + 主 Agent 复核完成（分类/光谱SAR大气/地形时序镶嵌分割）；70+ 全集未逐一展开（本轮聚焦缺陷驱动） |
| A02 NaN/Inf/NoData 契约 | VALIDATED(部分) | F-06（SAR 角域毒值）+ 快照 4 issue 复核（已修确认） |
| A03 分类与后处理语义 | VALIDATED(部分) | F-04、F-05 + 训练/验证独立性审查（代理确认 scaler 只在训练折拟合，无泄漏） |
| A04 分割与 OBIA | PARTIAL | 代理审查 Baatz/多尺度/连通性正确；simple segmenter 哈希碰撞记档 |
| A05 光学辐射与大气 | PARTIAL | radiometric/atmospheric 全链代理审查通过；fast_6s 0.5 系数存疑记档 |
| A06 SAR/光谱数值稳定 | VALIDATED(部分) | F-06、F-07 |
| A07 几何与网格契约 | VALIDATED(部分) | F-03（镶嵌 X 翻转 fail-closed） |
| A08 时序算法质量 | VALIDATED(部分) | F-01（趋势时间戳配对） |
| A09 地形水文计算 | VALIDATED(部分) | F-02 + 代理确认 D8/D∞/填洼/汇流/坡度单位无回归 |
| A10 分块算子开销 | N/A 本轮 | 无可证热点修复（不硬凑） |
| A11 取消与原子输出 | N/A 本轮 | #1544 已修未关（复核确认） |
| A12 多入口一致性 | N/A 本轮 | — |
| A13 大数据资源测试 | N/A 本轮 | — |
| A14 基准与回归收敛 | PARTIAL | F-07 附带大坐标条件数边界测试 |
