# R15-A REVIEW_ROUND2 — 测试充分性与锁诚实性评审

> 评审对象：`origin/master(46791bc6)...HEAD` tests/ diff（3 文件，+627 行）+ 生产代码失效推演。
> 方式：独立 subagent 只读评审（未修改文件、未构建、未提交）。
> 时间：M4 阶段，commit `96b85fef4` 时点（评审响应落地于 `362dc61af`）。

## 发现清单

| ID | 严重度 | 文件:行 | 发现 | 处置 |
|---|---|---|---|---|
| R2-1 | MINOR | test_image_fusion.cpp -9999 锁 | 对"移除 wash"不敏感：#611 负和 guard 同样产生 NaN（sum 4-9999 ≤ 1e-10） | ✅ 已记录（362dc61af）：注释说明盲区系构造使然，mild 哨兵姊妹锁承担该敏感性 |
| R2-2 | NIT | mild 锁 | 充分：16/16 全覆盖 + margin 1e-3 足够紧 + 三重回归（wash 移除/掺混/fixture 退化）均敏感 | 无需动作 |
| R2-3 | — (clean) | A14 RED 测试 | 无空转通道：fixture sanity 先行；同 CRS 下唯一能产出该错误文案的路径即 A14 guard；QTemporaryDir 排除陈旧文件 | 无需动作 |
| R2-4 | MINOR | both-unreferenced 回退测试 | 本地缺 fixture sanity 探针；若 GDAL 改为 VRT 继承源 GT 会静默退化 | ✅ 已修（362dc61af）：双侧裸 VRT 的 hasGeoTransform 探针 |
| R2-5 | — (clean) | [r14a] VRT 补 GeoTransform | 必要（不加则被 A14 拒）且与断言一致（同 GT → scaleX=1 → 断言不受影响） | 无需动作 |
| R2-6 | — (clean) | Fixture 真实性 | O1 三个 VRT 均带 GeoTransform+SRS+逐波段 NoData+SimpleSource，与 gdalbuildvrt 产物同构；A14 两处偏离均有成文注释 | 无需动作 |
| R2-7 | — (clean) | O2 双锁 | fixture sanity 已验；两断言逻辑互斥无法同假通过；clean-pixel 锚点防真空转；生产代码证实方言真实存在 | 无需动作 |
| R2-8 | NIT | O2 sam 侧 | 未钉住实际类别（语义即 data-vs-void，可接受） | 无需动作 |
| R2-9 | — (clean) | 遗留诊断代码 | 无；diag1-diag4 已在 O1 关闭提交中重写干净 | 无需动作 |
| R2-10 | NIT | 命名/tag 约定 | ① 冒号空格 ② OBIA 双重 [r15a] 标记 ③ fixture 复制粘贴可抽 helper | ① ✅ 已修（362dc61af）；②③ 记录接受（历史提交/NIT 级） |
| R2-11 | — (clean) | A09 锁（test_obia_segmentation） | epsilon 谓词吞 3 个 5e-7 合法样本 → 空 map → REQUIRE 红；精确谓词下全中 | 无需动作 |

**无 BLOCKER / MAJOR。**

## 锁失效能力专项（核心验证项）

| 验证项 | 结论 |
|---|---|
| O1 mild 锁可失效性 | ✅ wash 移除（pixel 0 不再 NaN）、bilinear 掺混（邻域偏差 ≫1e-3）、fixture-GeoTransform 退化（走钳位路径图案不匹配）三重回归均必然被抓 |
| O1 -9999 锁可失效性 | ✅ 对掺混/fixture 退化敏感；对 wash 移除盲（R2-1，覆盖由姊妹锁提供） |
| O1 质量报告锁 | ✅ meanRatio=5/3 期望值经累加器源码核实（成对有限性排除）；wash 移除经 band 1 使 ratio 漂移至 1.771 被抓 |
| A14 RED 非空转 | ✅ 见 R2-3 |
| 回退保持 | ✅ both-unreferenced 融合成功且 4/4 像素精确值 |
| [r14a] 一致性 | ✅ 见 R2-5 |
| O2 双锁非虚假 | ✅ 见 R2-7 |

## 裁决

# SHIP

五个核心验证项（锁可失效性、A14 非空转、回退保持、[r14a] 一致性、fixture 真实性）全部通过；两条 MINOR 与全部 NIT 为改进建议，不阻塞合入（其中 R2-1/R2-4/R2-10① 已在本轨响应）。
