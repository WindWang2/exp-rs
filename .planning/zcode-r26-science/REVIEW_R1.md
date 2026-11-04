# REVIEW_R1 — 独立评审（未参与实现的代理，全新上下文）

评审对象：git diff --cached（13 文件，+1097/-43，含 tests/test_r26_science_oracles.cpp 835 行新文件）
评审方式：只读；逐修复点深读 + 关键点实证复验（OpenCV copyTo 探针、洗牌同构核对、测试影响面核对）。

## Finding 汇总

### P0
无。

### P1（已修）
| ID | 问题 | 处置 commit |
|----|------|-------------|
| R1-P1-1 | S2 capped 路径中间内存无界化：cap 后置后 samples/sampleRank + 全局 order/sorted* 2-3 倍拷贝，使 GUI 默认 maxSamplesPerClass=5000 的内存边界失效 | 去除全局 order/sorted* 拷贝（改类内 (pixel,rank) 配对排序）；残余候选级 out.X 画像记入 PERF_BASELINE.md（彻底方案下沉 NoData 判断到 scanline 循环，超出 correctness 轨范围，建议后续 perf 轨） |

### P2（已修 / 记录）
| ID | 问题 | 处置 |
|----|------|------|
| R1-P2-1 | S4 陈旧重复注释块 | 已删除，保留准确表述 |
| R1-P2-2 | var(S) 两轴大量并列时可达负值并对外暴露 | `std::max(0.0, ...)` 钳制 + 新增 doubly-tied 测试断言（z=0/p=1） |
| R1-P2-3 | change_detector.h 头契约未记录新 log-ratio 语义 | 头文档同步（NaN 传播/负值钳制） |
| R1-P2-4 | S2 cap 后不可达的 keepRows.isEmpty() 防御码无注释 | 已加注释 |
| R1-P2-5 | S3 合并用精确 ==，未来任意 double score 可能漏合并 | fitIsotonic 输出前加 knot 严格递增 fail-closed 校验 |
| R1-P2-6 | S1 钳后死守卫 | 已加注释（与流式实现对称保留） |
| R1-P2-7 | RsPostProcess::polygonize 仍 4 连通，与 RsClassRaster 分叉 | 已一致化 8CONNECTED=8（golden E2E 大块区域不受影响，实测通过） |
| R1-P2-8 | tests/CMakeLists.txt 与 5 个开放 PR 同点尾部追加（潜在文本冲突）；S10 依赖 #1728 领地的 isGeographicCrs | 已记录 PR_COLLISION_MATRIX；冲突可保留双方块解；语义依赖已记录 RISK_REGISTER |
| R1-P2-9 | S11 判定（UNREPRODUCED）独立复验成立 | 确认 |

### 深读结论（R1 逐项复核无异议）
- S2 rank 穿线正确性：非 capped 分支不越界；三位一体同步；无 NoData 时选择 = 历史 shuffle 前缀（既有钉值测试通过实证）
- S3 monotonicity：等 x 合并为加权均值落在相邻块 rate 之间；排序保证等分块相邻
- S5 确定性：RNG 消费序与 round-robin 分配序均升序类 id；4 个调用方测试均为统计阈值不受影响；oracle 参考模拟与 kFold 洗牌同构
- S6：clusterRemap 空 → 0；y 含 0 需调用方违反 classId>0 契约，pre-fix value(pVal,0) 本有同类巧合
- S8：8CONNECTED 参数位正确；OBIA smoke 仅断言存在性
- S10：OSR 句柄无泄漏；地理走首分支；OSRIsProjected 对地理返 false 无碍
- S1：pct=50 行为与修复前一致；负/≥100 修复 OOB
- 跨平台：char* 选项数组 GDAL 标准用法；S5 消除 QHash 序风险；MSVC 无新增窄化告警

## 结论
**approve-with-fixes**（R1-P1-1 + P2 全批已处置，见上表 commit 列）。

## 修复后复验证据
- test_r26_science_oracles: 347 assertions / 13 cases PASS
- test_d16_temporal_trend: 84/4 PASS（Gilbert 基准 tau-a=42/45、varS=124 保留）
- test_training_data_extraction: 766/10 PASS
- test_rs_golden_e2e: 21/1 PASS（P2-7 一致性后）
- 其余 12 个受影响套件回归全 PASS（见 TEST_RESULTS.md）
