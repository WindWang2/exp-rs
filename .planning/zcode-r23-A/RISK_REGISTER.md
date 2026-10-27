# r23-A RISK_REGISTER

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R1 | 移除 bothNormalizable -999 魔数后,未声明 NoData 的原始 -9999 哨兵流入指数计算产生 ±1 垃圾值 | 中 | 中 | 全部生产调用方(operator 流式 readTileBand、raster_ndvi、spectral_index_algorithm readBandData、temporal reader)已核实预转 NaN;测试钉住行为契约;header 文档写明"声明 NoData 由调用方解析" |
| R2 | 与兄弟轨(B/C)在 tests/CMakeLists.txt 冲突 | 中 | 低 | append-only;push 前逐字节对照 origin/master 注册块 |
| R3 | 既有测试钉住旧 -999 行为导致 RED 误判 | 低 | 低 | 全仓 grep 测试中对 bothNormalizable/-999 的引用(test_math_utils.cpp 仅 computeStatsWithNodata 用 -9999 数组,与本内核无关) |
| R4 | 构建资源竞争(3 轨并行,-j 上限) | 中 | 中 | 独立 build-r23-A + 全局 flock + 默认 -j2 |
| R5 | 审计代理误报导致修复引入回归 | 中 | 中 | 每项修复必须先构造可复现反例(RED)再动源码;R1/R2 独立评审复核 |
| R6 | master 在执行期间前移 | 中 | 低 | push 前 fetch+diff 复核;无交叉则 merge;有交叉语义合并 |
| R7 | #1687-1 行为变化(同值负反射率对从 NaN→0)破坏下游消费者 | 低 | 低 | 全仓 grep 消费者:输出为 float NaN 语义一致;0 与 NaN 在下游统计中均被计为有效/无效取决于调用方——见 DECISIONS D1 语义分析 |

## 追加(审计后)
| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| R8 | viewshed 环改按 min(cellSize) 量化后 maxRing 增大(内存/耗时按比例) | 低 | 低 | ringStart 为 size_t 向量,现实 DEM(格网尺寸比 ≤10×)增量有限;测试覆盖各向异性 |
| R9 | 曲率 zxy 修复改变 curvature_profile/plan 输出值(下游对比旧结果者会看到差异) | 中 | 中 | 这是修正错误值;header 公式同步改;解析 oracle 钉住正确值 |
| R10 | T2a/T3a(时序 suspicious 项)误修引入回归 | — | — | 不修,仅登记;R1/R2 评审复核决定 |
| R11 | VCA 相对门限过松导致把共线方向当端元 | 低 | 低 | 相对门限 1e-12·锚 norm²(长度比 1e-6);GS 门限仍拒绝塌缩方向;退化单元体测试保持拒绝 |

## 追加(suspicious 项复核,2026-10-27)
| R12 | rs_temporal_seasonal_breaks_operator.cpp:485-500 CI 对 fit.breaks[j].magnitude(分段模型统计量)自助,但 magBufs 写 attribution.breaks[j].trendMagnitude(朴素重拟合水平跳变);robust=true 时点估计可能落在自身 CI 外 | 中 | 低(诊断带语义) | 不修(未构造出数值反例;修复需统一统计量选择,涉及产品语义决策);建议后续轨与 B 轨 owner 协商 |

## R1 处置(2026-10-27)
- R1-P2-1 dNBR 缺端到端钉:已修——test_spectral_formula_drift 新增未声明 -9999 场景(operator 级);变异验证:缝回退→该测试 RED(exit 42),恢复→155 断言全绿。
- R1-P3-1 #1687 与 seam 提交回退耦合:PR body 注明"cc34cb4ed6 与 d4f284c6b4 需成对回退"。
- R1-P3-2 VCA GS 门仍绝对 1e-12:接受并登记(较 master 纯放宽、无回归;未来可改 1e-6·sqrt(anchorNorm2))。
- R1-P3-3 table 拒绝静默:已修(digestHex/toJson 拒绝时 qWarning)。
- R1-P3-4 极区带阈值未入头文件:已修(terrain_solar.h 契约补 1 句)。
- R1-P3-5 planning 状态陈旧:PR 前统一更新(TASKS/ledger 终态)。
- R1-P3-6 staged-vs-fused 未声明哨兵分歧:#1687 显式设计决策,PR body 一行说明;change_detection.cpp:49 参数交换观察留后续轨。
