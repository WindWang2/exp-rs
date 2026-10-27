# R24-A REVIEW R1（独立评审，闭环记录）

评审员：全新 general-purpose 子代理（与实现者零共享上下文），只读，对 8 提交 20 文件 +1600/−17 做对抗性复核（全调用点核对 + 手工数学复算）。
**结论：APPROVE-WITH-FIXES**（0×P0，1×P1，1×P2，7×P3）。

## 发现与闭环

| 编号 | 严重度 | 发现 | 处置（commit 逐项） |
|---|---|---|---|
| P1-1 | P1 | 等值负对语义反转击穿既有注册测试 `tests/test_image_fusion.cpp:1930`（`CHECK_FALSE(bothNormalizable(-1,-1))` 钉旧契约），本分支该二进制必红——影响面核查遗漏 | ✅ 钉子翻转为新语义并注明 #1687 反转（其余 -9999/-999/NaN/Inf 断言原样保留）；test_image_fusion 两轮全绿 |
| P2-1 | P2 | phenology_multi 重复时刻报错 off-by-one：违规在 (s-1,s) 却打印 (s,s+1)（指认未重复的景），且 `scenes().at(s+1)` 在**末位重复**时越界逃逸 std::out_of_range——缺陷从 decompose 逐字镜像复制（master 既有） | ✅ 三算子（decompose/smooth/phenology_multi）统一改为报实际冲突对（1-based 标签 s/s+1 ↔ 0-based s-1/s）+ 措辞改 "duplicate or out-of-order instant"；新增末位重复测试用例（断言类型化错误 + 消息含实际冲突日期 + 无 out_of_range 逃逸 + 无半成品） |
| P3-1 | P3 | viewshed 注释的"轴向内邻必降环"论证错误（反例：格 (3,4) 的 y 轴内邻 (3,3) 同环 5）；真实不变量是对角内邻（a,b≥1）+ 轴上格轴向内邻 | ✅ 两处注释改为正确不变量（对角为主、轴上退化），并警示不得把 pass-3 扫描收窄为仅轴向 |
| P3-2 | P3 | 环号 `ceil(d/minCell)` 无 int 溢出守卫（极端各向异性 1e4:1 + 16384² 可超 INT_MAX → UB） | ✅ `d/minCell > INT_MAX-2` 时 return false |
| P3-3 | P3 | dist 数组 float→double 使 2²⁸ 格预算 +1 GiB，角度 math 仍是 float 精度 | ✅ 回退为 float（与 master 原始 distMap 同型）；perf 复测仍快于基线 |
| P3-4 | P3 | D8 门控的 epsilon 扰动不可观测（flatReport 被丢弃，flat_resolve 产品却输出） | ✅ D8 产品 result JSON 增加 `flatResolution`（"epsilon"/"none"）+ `flatEpsilon`/`raisedCells`（解析时） |
| P3-5 | P3 | "share an instant" 文案对乱序/日粒度折叠过宽 | ✅ 随 P2-1 改为 "duplicate or out-of-order instant" |
| P3-6 | P3 | 哨兵重载无生产调用方，存在双轨并存风险 | ✅ 头注释明确"预留内核级组合；生产算子经 BandSource 预归一化达成同一掩码，不得叠加第二层归一化" |
| P3-7 | P3 | 测试盲区：缺末位重复用例；缺 NoData 边界平地钉子 | ✅ 末位重复用例（P2-1 一起）；NoData 岛钉子——**实证后修正预期**：resolveFlats 种子（邻 NoData 的 8 个边格）永不被抬升保持 dir=0 汇，内部格获 epsilon 梯度指向边界（排水止于 NoData 边界，不虚构流入）——按真实契约钉死 8 边格 |

## 已核未见问题（评审员逐项结论，摘录）

- 30 处 normalizedDifference/bothNormalizable 调用点逐一核对：语义变化在所有调用方一致且为 #1687 意图；dNBR 融合核与 staged 掩码契约保持。
- 哨兵重载与 nodata_utils.h 纪律（精确相等/非有限禁用/非有限恒无效）一致；src 内无游离 -999 字面量。
- viewshed 无孤儿结论（除注释依据错误外）严格数学成立（含各向异性，评审员独立证明）；radius/退化输入核查无越界。
- D8 门控早退/NoData 双侧跳过/巧合相等仅多付一次解析/下游产品掩码一致/fill·flat_resolve·flow_direction_inf 未被二次门控/取消语义一致。
- phenology 守卫确在输出创建前；SAR 守卫在 GDAL open 之前（贵 I/O 前拒绝），NaN/Inf 同谓词拒绝。
- Whittaker 金标值评审员独立求解复核为真最优；SAR 35° 阳性对照证 fixture；perf harness 明示非门禁。
- 资源/异常：D8 峰值多一帧在估算内；守卫零分配；catalogKeys 静态初始化线程安全。

## 闭环验证

R1 修复轮后两轮全绿（12 靶 + 补跑）：test_image_fusion、test_terrain_viewshed（1453 断言）、test_temporal_operators_10、全部 9 个 R24-A 靶（含新增 2 用例）。日志 r1fix1_/r1fix2_*。
