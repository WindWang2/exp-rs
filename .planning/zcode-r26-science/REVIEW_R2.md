# REVIEW_R2 — 独立复审（新上下文，未参与实现与 R1）

评审对象：commit 9fdf8d8aaa（R1 修复后提交）+ 其后 R2 修复提交。
评审方式：只读；R1 finding 逐项独立复核 + 重点代码深读 + oracle 期望值独立手算 +
穷举统计验证（Mann-Kendall 双轴并列）+ 开放 PR changed-files 只读比对。

## 结论
**approve**（无 P0/P1；9 条 P2，其中 6 条已在复审同批处置，3 条记录在案）。

## P2 处置
| ID | 问题 | 处置 |
|----|------|------|
| R2-P2-1 | 双轴并列时两矩公式低估 var(S)（穷举实证：n=4 x=[0,0,1,1]/y=[1,1,2,2] 精确 16/3 vs 公式 14/3）→ p 偏小（轻微反保守） | 已在 trend_analysis.h 头文件文档化为近似（修复前忽略时间并列为过保守，本修复在所有区间仍严格改进） |
| R2-P2-2 | 测试注释算术错误 (156-144-144) 应为 (156-156-36)=-2 | 已更正注释 |
| R2-P2-3 | 测试 banner 称 "tau must be tau-b" 与实现/断言（tau-a）矛盾；:342 注释错 | 已更正为 tau-a 表述与正确算术 |
| R2-P2-4 | "class 1 有 128 valid pixels" 不准确（实为 240 候选/120 valid） | 已更正 |
| R2-P2-5 | 8 连通对角接触产自接触 ring，OGR_G_IsValid=false | 已实证并固化行为（断言 !IsValid + 单 ring + 要素数 1），docs 记录"自接触是真实拓扑，下游需 makeValid 而非拆对象" |
| R2-P2-6 | scaler fromJson toInt(-1) 类型混淆：非数值 JSON → 0 静默按 ZScore 接受 | 已修：isDouble() 前置校验 |
| R2-P2-7 | 工程/局部 CRS 下 changedArea 单位缺口 | 记录为既有范围问题（S10 覆盖地理/投影米/投影非米三态），不在本轨扩围 |
| R2-P2-8 | rs_training_data_extraction.h:93 头契约仍写 std::shuffle | 记录（capped 内存画像已在 PERF_BASELINE，彻底方案留 perf 轨） |
| R2-P2-9 | apply() x[hi]==x[lo] 死分支 | 无害防御码，保留 |

## R1 处置的独立复核（全部成立）
- S2 新 capped 写法与旧写法语义等价（pixelGroup 同键同值、rank 类内唯一、
  (class,pixel) 全局升序、无 NoData 时选择=洗牌前缀、RNG 消耗序列一致）
- Gilbert 基准位保持（时间唯一时 timeTieTerm=0；float→double 保序）
- S3 fail-closed 无误杀（float 扩宽 double 语义：相异 float→相异 double）
- polygonize 8CONNECTED 在 mask=nullptr 与 GPKG/Shapefile 两驱动下安全
- oracle 期望值独立手算正确（S2 10/8、S3 [0.6,0.4]、S10 4×ft²）；无测试间状态共享
- 与 #1724-#1728 零生产冲突（仅 tests/CMakeLists.txt 机械文本冲突）
