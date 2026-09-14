# REVIEW_LOG — advanced-sar-polsar-insar-10

## Phase 7 · 对抗 review（2 个只读 subagents，2026-09-14）

### Subagent B — 性能 / 内存 / 并发 / 生命周期 / 测试可信度（已完成）

范围：`git diff origin/master..HEAD` 全部内核、算子、测试；交叉验证 GDAL 流式 IO 生命周期与平台纪律。
结论：**P0 = 0**；P1×3、P2×2、P3×5。

| ID | 级别 | finding（摘要） | 验证 | disposition | evidence |
| --- | --- | --- | --- | --- | --- |
| F1 | P1 | PhaseRampFitter 水库是"前 65536 截断"，与头文件声称的采样语义不符——真实尺寸干涉图的 flattenRamp 系统性偏差（只拟合顶部条带） | 属实（`SampleReservoir::add` 无采样逻辑） | **fixed**：改为确定性 LCG 水库采样（Algorithm R，固定种子），每个样本等概率入选；契约文本同步改为 "deterministic-reservoir-sampled" | sar_insar.h SampleReservoir；编译+test_sar_insar 全绿 |
| F2 | P1 | unwrap 2 GiB plane 预算算式漏项（quality 平面 + 写出 float 拷贝 + 优先级队列），门可通过但真实峰值最高 ~3× | 属实（算式只有 17wh） | **fixed**：预算算式改为 21wh + 8wh(quality) + 48wh(队列半权重)；头文件 memory contract 同步记录队列上界 | rs_sar_unwrap_operator.cpp gate |
| F3 | P1 | 全平面内核（unwrap/coregistrationShift）无取消点——违反"所有栅格路径可取消"；且 coregister 合法参数（R=64,P=128,stride=1）可放大成天级不可杀任务 | 属实（内核签名无 context） | **fixed**：两内核加 `cancelProbe` 回调（unwrap 每种子+每 4096 pop、NCC 每 16 patch）；coregister 算子加复杂度预算门（ops > 2e10 → InvalidParameter 拒绝 + 指导） | sar_insar.cpp、rs_sar_coregister_operator.cpp |
| F4 | P2 | unwrap 多种子时每分量全平面重扫 O(components·n) 退化 | 属实 | **fixed**：有效像素按种子序一次性排序，游标推进取种子——O(n log n) + O(n)，确定性不变 | qualityGuidedUnwrap seedOrder |
| F5 | P2 | 无任何跨 tile + halo>0 的流式测试（tile 接缝映射零覆盖） | 属实 | **fixed**：新增 5×4 栅格 / 2×2 tile / halo=1 的逐格（core+halo clamp 语义+sentinel 复制）精确性测试 | test_sar_complex "multi-tile with halo"（378 断言全绿） |
| F6 | P3 | flattenRamp≠none 时 coherence 用未去 ramp 的原始缓冲（行为未文档化） | 属实 | **fixed**：先整 tile 展平（flatM/flatS），干涉图与相干性消费同一展平场 | rs_sar_interferogram_operator.cpp |
| F7 | P3 | displacement Itoh 边界条件恒真，栅格最末行/列自配对轻微压低 ratio | 属实 | **fixed**：两成员都必须在 core tile 内 | rs_sar_displacement_operator.cpp |
| F8 | P3 | unwrap/coregister 的 estimateExecution 恒返预算常数（非真实分配） | 属实 | **accepted-debt**：预算门算子以"上界估计"语义呈现是合理取舍；dynamic 估计对 plane 算子意义有限。记录于 PR_BODY known limitations | REVIEW_LOG 本行 |
| F9 | P3 | 测试遗留调试 printf | 属实 | **fixed**：已删除 | test_sar_insar.cpp |
| F10 | P3 | polsar ensemble O(window²)/像素在 windowSize=101 时代价高（无时间代价提示） | 属实（设计取舍） | **fixed (documentation)**：operator metadata limitations 增加 O(window²) 成本说明；窗口上限仍强制 | rs_sar_polsar_decompose_operator.cpp |

Subagent B 明确"已验证无问题"的点：tile 级算子内存界限与参数上限、#647 abandon 语义（含 closeWithError 三分支）、GDAL 单线程 twin-read 纪律（与 rs_sar_temporal_stats 先例一致）、无 -j$(nproc)/无界并行、E2E 走真 registry、refusal 断言错误码文本、known-answer 测试无循环断言。

### Subagent A — 架构 / 科学与语义正确性（已完成）

范围：5 个内核 + 7 个算子逐文件精读；对照教科书逐项推导核对全部核心公式；三处（头文件/metadata/docs）契约一致性核对。
结论：**P0 = 0**；数学内核全部正确（H/A/α、FD、Yamaguchi helix、Jacobi、干涉/相干/Goldstein/LOS/Itoh/NCC 逐项推导核对通过）；dual-pol 绕过路径封死；provider 拒绝 fail-fast；NaN 纪律与架构一致性无问题。P1×3、P2×3、P3 组×1：

| ID | 级别 | finding（摘要） | 验证 | disposition | evidence |
| --- | --- | --- | --- | --- | --- |
| A-F1 | P1 | flattenRamp 下干涉图幅度 |s1\|·\|s2\|²（flatM 幅度把 |s2| 也乘入后又被 conj(s) 乘一次）；修复初稿仍错（对相位差再减 ramp），最终以 flatM = master·e^{−i·ramp} 修复 | 属实；且第一轮修复仍不正确，经独立复现程序（rampdbg）实证后二次修复 | **fixed**：flatM=master·e^{−i·ramp}、flatS 原样；新增 ramp≠none 的 E2E（幅度=6 精确 + 残余相位 0，2766 断言全绿） | rs_sar_interferogram_operator.cpp flatten 块；test_sar_platform10 "flattenRamp keeps amplitude" |
| A-F2 | P1 | PhaseRampFitter 前 65536 前缀拟合 | 属实（与 Subagent B F1 同源） | **fixed**（同 B-F1：LCG 水库采样） | sar_insar.h |
| A-F3 | P1 | 契约承诺的 assumeReciprocity=false → NON_RECIPROCAL_CHANNELS 拒绝缺失，4 通道被静默塌缩 | 属实 | **fixed**：schema 增加 assumeReciprocity 参数；4 通道且 =0 时 typed 拒绝 NON_RECIPROCAL_CHANNELS | rs_sar_polsar_decompose_operator.cpp |
| A-P2-1 | P2 | ramp 预 pass 索引漏加 halo——拟合样本空间平移 −(r,r) 且混入边缘复制行，残留常数偏置 | 属实 | **fixed**：预 pass 索引改为 (y+halo)·bufferWidth+(x+halo)；ramp E2E 覆盖（coherence+ramp 同开） | 同上 |
| A-P2-2 | P2 | D-011 承诺的干涉基线几何（B⊥/B∥）零实现（僵尸契约） | 属实 | **fixed**：`interferometricBaseline`（B∥、B⊥、\|Δr\|、unit-LOS 校验）实现于 sar_orbit.{h,cpp} + 闭式 known-answer 测试 | sar_orbit.h 尾部；test_sar_orbit.cpp |
| A-P2-3 | P2 | quality 平面全 NaN 时 unwrap 静默输出全 NaN "成功" | 属实 | **fixed**：finite phase 存在但 unwrappedCount==0 时算子 typed 拒绝（指向 quality 平面） | rs_sar_unwrap_operator.cpp |
| A-P3-2 | P3 | Yamaguchi fv 可为负（强 helicity） | 属实 | **fixed**：fv 钳 0（并入已文档化的 SPAN-break 说明） | sar_polsar.cpp |
| A-P3-4 | P3 组 | 7 小项：D-006 系数未报告（fixed：rampCoefficients+rampCoefficientOrder）；D-009 键名 discontinuityRatio→phaseDiscontinuityRatio（fixed）；波段参数 camelCase vs D-002 snake_case（DECISIONS D-002 已修订记录）；显式 vhBand 不置 reciprocalAssumed（fixed）；pauli/h_alpha 的 SICNU_SAR_DOMAIN 标注（fixed：仅 power 产物标注）；日期未校验月长（fixed：月长+闰年校验）；D-004 论证未提 jacobiEigen（DECISIONS D-004 已补记） | 属实 | mixed：5 fixed + 2 DECISIONS 文字修订 | 各对应文件 + DECISIONS.md 评审后修订节 |
| A-P3-3 | P3 | unwrap 预算低估 | 属实（同 B-F2） | **fixed**（同 B-F2） | — |

### 守卫测试基线核对（pre-existing on master @ 7d78059，OUT_OF_SCOPE）

- `test_capability_drift`：cartography:diff_templates/explain/export 与 rs:gaofen/zy3/hj_import 的知识条目缺失——master 的 tools.json/io.json 即无这些条目（已核对 `git show origin/master`）；归属 Track 02（product import）与 cartography/tools 所有方。
- `test_capability_drift` recipe 别名 canary（harness.optical_ndvi_landsat）与 `test_mapspec` 一处制图视觉用例 SIGABRT：与本 track diff 无交集（制图/配方域），判定为 master 预存在/环境性，不在本 track 修复。
