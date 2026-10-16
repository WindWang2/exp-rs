# R15-A FINAL_REPORT — 遥感科学算法后合并收敛（轨道 A）

> 分支：`hardening/r15-A-science-processing`（worktree `exp-rs-r15-a-20261016`）
> BASE：`origin/master = 46791bc6`（2026-10-16，Merge PR #1509）｜ HEAD：`362dc61af`｜ 提交数：17
> 所有权边界：✅ 全程合规——仅写 `src/analysis/**`、`src/processing/algorithms/**`、`src/operators/rs/**`、`tests/**`、`.planning/r15-a/**`、`.gitignore`（白名单 3 条规则）；**`src/processing/gdal/` 与 `src/data/` 零修改**（两轮独立评审确认）。

## 1. 执行摘要

| 工作包 | 结论 | 证据 |
|---|---|---|
| **O1** 融合 resample-then-mask 哨兵掺混 | ✅ **FALSE POSITIVE**（实证重定性） | diag1–diag4：RED 模式系 fixture 缺 VRT `<GeoTransform>` → GDAL 默认单位变换 → 同分辨率钳位路径；正确 fixture 下 BASE 行为已符合家族惯例 |
| **O2** sam/spectral_similarity NoData 方言 | ✅ **双锁关闭，不统一** | 统一将静默改变既有未声明波段分类输出，超出重构纪律；双锁测试 + metadata 文档 |
| **A03-6**（新发现，提交别名 A14）单侧缺 GeoTransform | ✅ **已修复**（真实缺陷） | RED→GREEN×2：非对称配对原先静默产出大面积 void 垃圾 |
| **A09** multires 有效性谓词 epsilon 匹配 | ✅ **已修复** | RED→GREEN×2：契约精确比较替换 |
| **A02/A03/A06/A10/A13** 静态审计 | ✅ 完成，无存活缺陷 | FINDINGS.md |
| **O3/O5**（#1465 / #1509） | ✅ 关闭 | LEDGER L2/L3 |
| **O4**（#1454 回调侧） | ⏳ PENDING（未进入本轨循环） | LEDGER L6 |

## 2. 生产 diff（3 文件，+41/-3）

| 文件 | 改动 | 性质 |
|---|---|---|
| src/processing/algorithms/image_fusion.cpp | A03-6 守卫（8 行）+ `%.6g` 日志修复（驱动） | 行为收紧（拒绝非对称配对）+ 日志正确性 |
| src/analysis/segmentation/rs_multires_segmenter.cpp | 有效性谓词 ε→契约（1 行 + 注释） | 缺陷修复（严格收窄 void 集） |
| src/operators/rs/rs_sam_classify_operator.cpp | metadata() limitations 政策文档（5 行） | 纯文档 |

## 3. 测试 diff（3 文件，+645）

| 文件 | 内容 |
|---|---|
| tests/test_image_fusion.cpp | O1 三锁重写（GeoTransform+SRS 真实 fixture：pixel-0 void + 15 像素精确 brovey 值；质量报告 meanRatio=5/3）；A14 两例（拒绝 + 回退保持，均带 fixture sanity）；[r14a] fixture 补 GeoTransform |
| tests/test_spectral_classification.cpp | O2 双锁（同一 3×1 fixture：sam `labels[2] != -9999` vs similarity `labels[2] == -9999`，互斥断言 + fixture sanity） |
| tests/test_obia_segmentation.cpp | A09 RED 锁（9187cdc79） |

## 4. O1 重定性——本轮最重要的科学结论

**原假设**：GDAL RasterIOEx 双线性对声明 NoData 的波段不感知，哨兵被掺混成有限污染值。
**实证**（diag1–diag4，TEST_RESULTS.md）：本环境 GDAL 的 GRIORA_Bilinear **是** NoData 感知的——排除空值 tap 并重归一化（与 resampler.cpp 家族惯例、GDAL 原生行为、rs_simple_segmenter 一致）；空值仅在主导 tap 处透传并被精确匹配 wash 捕获。RED 模式 `{NaN,6.667,NaN,NaN,6.667,6.667,NaN×10}` 的成因是测试 fixture 的 VRT 缺 `<GeoTransform>`（GDAL 填默认单位变换 `[0,1,0,0,0,1]` → 融合走同分辨率钳位路径 → readBandWindow 钳位 + 哨兵填充）。正确 fixture 下 BASE 输出 `{NaN,6.667×15}/{NaN,3.333×15}`。

**衍生真实缺陷（A03-6）**：正是该调查暴露——pan 有地理参照而 MS 无时，`compareGrids` 的"任一侧无 GT → 无 issue"回退与融合的单位变换默认值叠加，产生**静默垃圾产出**。已修复并锁定。

## 5. 质量门禁

| 门禁 | 状态 |
|---|---|
| RED→最小修复→GREEN×2→记账循环 | ✅ 每个修复包完整执行 |
| 测试（3 套件） | ✅ 36/1615、30/197、14/850 全绿，各 ×2 |
| 两轮独立评审 | ✅ R1（生产正确性）SHIP；R2（测试充分性）SHIP；响应落地 362dc61af |
| 性能 | ✅ 无热路径改动（PERFORMANCE.md） |
| 所有权边界 | ✅ 两轮评审独立确认 |
| 重构不改行为纪律 | ✅ O1/O2 无生产行为变更；A03-6/A09 为缺陷修复（行为收紧/纠正），均有测试锁定 |

## 6. 残留与后续建议

| 项 | 说明 |
|---|---|
| O4（#1454） | 未进入本轨循环，建议后续轨道 |
| A12 定义缺口 | 工作包枚举中缺失（冻结文档无定义），未虚构范围 |
| both-unreferenced + 尺寸失配 | compareGrids 文档化回退仅做维度检查，融合未阻塞该组合的钳位垃圾——记录在案，建议后续统一治理（compareGrids 调用方公约） |
| VRT fixture 经验 | 任何 VRT fixture 必须携带 GeoTransform + SRS，否则触发单位变换陷阱；已写入测试注释 |

## 7. 提交清单（origin/master..HEAD，17 个）

规划/记账：af488d482（M0 冻结）、16e095a01、80b1e468b、cffc966d9、a77e9ecde、530a566b5、005a39b13
测试/修复：7aae63038→6d5672419（O1 RED→重定性关闭）、b4a7d148b→（同上）、9187cdc79→28a1445c5（A09 RED→修复）、96b85fef4（O2 双锁）、bf5a7d27a（A03-6 守卫 + 日志修复）、362dc61af（评审响应）
