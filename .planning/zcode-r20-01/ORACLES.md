# ORACLES — zcode-r20-01

每个修复的失败 oracle（测试如何证明 bug、为什么不是恒真）。

## F-01 趋势时间戳配对（P1）

- **oracle**：解析真值 + 双入口一致性。t={0,10,20,30}, y={1,NaN,2,3}：幸存对 (0,1),(20,2),(30,3)，逐对斜率 {0.05, 2/30, 0.1}，中位数 **2/30≈0.0667**；master 现码（buggy）用 ts 前缀 {0,10,20} 配 {1,2,3} 得全 0.1。另断言单序列入口（正确实现）=批入口逐像素一致。
- **防恒真**：变异探针——若把修复回退（ts 前缀配对），oracleSlope 断言从 0.0667→0.1 必红；第三个 SECTION（未排序轴）在 buggy 下 0.0222 vs 正确 0.02 亦可分辨（margin 1e-6）。

## F-02 viewshed NoData 哨兵（P2）

- **oracle**：头文件契约直接断言——15×15 平面 + 56 像元 collar + 1 内部洞，nodataByte=255 时每个 NoData 像元 ==255、非 NoData 像元 !=255、analysed==168；nodataByte=0 时 NoData 像元 ==0（字母表回退）且 cumulativeViewshed 行为不变（NoData 计数==999，有效像元 ≤1）。
- **防恒真**：master 现码 NoData 像元恒 0 → `==255` 断言必红；`analysed==168` 在 buggy 下为 225（全部计入）。

## F-03 镶嵌 X 翻转（P2）

- **oracle**：ref gt[1]=+1 与 flip gt[1]=-1 两场景计划 → 断言 diagnostics.xDirectionMismatch=={1}、scenes[1].gridEligible==false、placement.valid==false、warnings 含 "X-axis orientation mismatch"；对照场景（+1/+1）无诊断且 eligible。
- **防恒真**：master 现码无该诊断向量（编译失败）或（对预修复源码）gridEligible==true → 断言必红。对照 SECTION 防止"所有场景都被拒"的过拟合修复。

## F-04 MLP 单类 classOrder（P2）

- **oracle**：后端契约（rs_classifier_backend.h:68-72 "非空必严格递增"）+ sidecar round-trip。单类 {7} 训练 → classOrder 必须 isValid 且 size==1 且 front==7；RsClassOrder::toJsonArray→fromJsonArray round-trip 必须成功。
- **防恒真**：master 现码返回 [7,7] → isValid false 必红。round-trip 在 buggy 下 fromJsonArray 返回 false 必红。OpenCV 5.0.0 ANN_MLP 拒绝训练时测试显式 SUCCEED（不制造假红）。

## F-05 ISODATA y 守卫（P3）

- **oracle**：fail-closed 契约（镜像 KMeans 同构守卫）。CV_32F 单列 / Nx2 CV_32S / 行数不匹配 / (rows>0,cols==0) 四种畸形 y → fit 返回 false 且 !isFitted；空 y → fit 成功且 !needsLabelRemap（哑输入语义不变）。
- **防恒真**：master 现码对 CV_32F/Nx2/短 y 静默接受（fit true）→ REQUIRE_FALSE 必红。(rows>0,cols==0) 在 buggy 下解引用空 data（可能崩溃/未定义）——本身就是缺陷证据。

## F-06 SAR 入射角域（P2）

- **oracle**：物理域 + 解析值。θ∈{30°,89.99°} → gamma0 解析值（sigma0/cosθ）；θ∈{90°,0°,-5°,120°} → NaN；常量 θ0=90.0°（R1-3 改定的判别探针）→ 全 NaN。含真实 GTiff 流式 round-trip（writeGdalOutput→convertBackscatterRaster→readBandData）。
- **防恒真**：master 现码 θ=90 输出 ~1.6e16 有限值 → isnan 断言必红；θ=120（cos<0 → 负 → finishValue 拒）在 buggy 下也 NaN——该输入是行为不变项，非 RED 主体。
- 纯函数 sigma0ToGamma0(2.0, 90.0) > 1e15 的既有断言**保持不变**；operator 缝 refusal 见 R2-1 测试（90/95/700/sameState 边界）（该测试钉的是纯数学函数行为，修复在栅格 kernel 的域检查）。

## F-07 InSAR 斜坡条件数（P2/P3）

- **oracle**：已知系数曲面恢复 + 平移不变性 + 退化秩亏 fail-closed。c=[0.5,1e-4,-3e-5,2e-9,1e-9,5e-10]，[0,20000]² 100×100 格点：线性系数相对误差 <1e-6、二次 <1e-4；同一格网平移 +100000 后系数不变；evalPhaseRamp 在未采样探针点复现曲面（margin 1e-9）；全部样本在 y=x 线上时 fit 仍拒绝。
- **防恒真**：master 现码原始坐标正规方程 x⁴~1.6e17 vs n~1e4（条件数≥1e13 beyond double 有效位）→ 系数误差远超 1e-4 相对容差（或 invert 误判奇异 fit false → REQUIRE 必红）。小网格既有测试（20×15 精确恢复）继续通过证明修复未破坏小场景。

## 记档提案（无可靠 oracle，本轮不修）

- fast_6s_lookup.cpp 水汽 0.5 不对称系数：需要 6S/MODTRAN 权威横对照；建议后续以独立物理参考 issue 化。
- image_fusion 内存核与流式路径的样本集/Inf 门不一致：建议统一到流式洗刷语义（行为变更需金样本对照）。
