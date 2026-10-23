# REVIEW_R2 — zcode-r20-01

评审员：独立 fresh reviewer R2（general-purpose 代理，与 R1 及实现者不同，只读），2026-10-23。
对象：`git diff origin/master...HEAD` 全量（7 commit，+993/−14，25 文件）。
方式：全量 diff 走读 + 消费方/兄弟代码追踪 + 心算变异探针 + 合并冲突面预判。

## 总体结论：**7 项科学修复本体全部复核通过**（含 F-07 backConvert 六系数逐项手工展开复核、operator 边界默认 0/sameState 无误拒、Float32 oracle margin ~4×余量、R1 的 4 项修复未引入新问题）。无 P0/P1。需小修 4 项后合并。

## Findings 与处置

| # | Sev | 内容 | 处置 |
|---|---|---|---|
| R2-1 | **P2** | operator 缝 `incidenceDeg>=90` typed refusal 零测试覆盖（回退校验全套件仍绿） | ✅ 已修：test_sar_operators 新用例（90/95/700 拒绝 + sameState 默认 0 放行边界 + sameState+120 仍拒）；910 断言/29 用例绿 |
| R2-2 | P3 | 同族 y 守卫只补了 ISODATA；MLP（同文件！）fit 裸读 `y.at<int>`；SVM/NB/statistical 同病 | ✅ MLP 已修（type/cols 守卫 + 2 用例，21/4 绿）；SVM/NB/statistical 走 RsClassifierCvBackend 基类路径（有 rows 校验），type 校验记 follow-up（CROSS_TRACK 级技术债，涉及 4 个后端统一重构） |
| R2-3 | P3 | 死代码 rampDesignRow（-Wall/-Wextra 告警源） | ✅ 已删 |
| R2-4 | P3 | PR_BODY 范围声明漏 operator 文件 + 证据文件被 .gitignore 挡 + ORACLES θ0=95 陈述过期 + LOOP_LEDGER R6 未闭环 | ✅ 本 docs commit 补正：TEST_RESULTS/BENCHMARKS/ORACLES/LOOP_LEDGER/PR_BODY 全部 add -f 入库并填实 |
| R2-5 | P3 | raster 入射角路径把 constantIncidenceDeg（恒 0/负）写进输出元数据 | 📝 记档 follow-up：元数据缝跨 sicnu::sar 多个写入方（calibrate/speckle/geocode 共享 writeSarOutputMetadata 契约），单独改一个分支会造成元数据不一致；建议后续统一"raster 路径不写常量角"提案 |
| R2-6 | Info | ISODATA 守卫拒绝重拟合时旧模型残留（与 KMeans 现状同构的既有模式） | 📝 记档（非本 diff 引入） |

## R2 复核确认的关键点（摘要）

- operator 边界：默认 incidenceDeg=0 + sameState → 不误拒；cross-state + raster → 逐像元域检查管；sameState+显式≥90 拒绝是刻意 fail-closed（该角会原样写进输出元数据）。
- 异常矩阵：趋势（空/单帧/全 NaN/重复时间戳/非有限 tDays）、viewshed（NoData 观测点拒绝/ring=-1 不可能覆写哨兵/cumulative 语义不变）、mosaic（gt[1]=0 前置检查已拒/参考系翻转对称 fail-closed）、MLP（连续去重精确=去重，sidecar 自洽）、InSAR（退化跨度→秩亏 fail-closed/求和无溢出）——全部 fail-closed 或语义正确。
- 变异探针：趋势回退→0.1 vs 0.0667 必红；viewshed 回退→首 collar 像元即红（双向断言+计数防写反）。
- 合并冲突面：origin/master 近 35 提交与本 diff **零同文件重叠**。
- Commit 信息与文件清单一一相符，无夸大。

## 处置后复跑

R2-1/2/3 修复 + MLP 测试后：9 目标（7 本轨 + test_sar_operators + test_sar_radiometric_state）连续两遍全绿（TEST_RESULTS.md）。
