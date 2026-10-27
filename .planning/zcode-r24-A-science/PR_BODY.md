# fix(r24_a): 遥感科学算法数值正确性 — NoData/负反射率语义、viewshed 环归一化、D8 平地路由、时序守卫与 SAR 角域（RED→GREEN×2，双轮独立评审）

R24-A science 轨。base `master@5fe642ec6339807823aa56612ba94668592a738d`（执行时刻远程事实基线；Open PR=0，144 open issues 已全量再分诊，FIXED_IN_MASTER 重做=0）。
head `agent/r24_a-20261027T022732Z`（15 个提交，含 R1/R2 两轮评审修复）。

## 修复清单（每项 RED→GREEN×2，详见 `.planning/zcode-r24-A-science/RED_GREEN.md`）

| # | 缺陷 | 修复 |
|---|---|---|
| 1 | **#1687**：`bothNormalizable` 的 `a==b&&a<0` 条款把大气校正后合法的等值负反射率对（a=b=−0.01）NaN 化——(a−b)/(a+b)=0/(2a)=0 有定义；`<=−999` 魔数无单一所有者；`computeStatsFromAccumulators` validCount 语义未文档化 | 移除等值负对拒绝；`kUndeclaredSentinelCeiling` 命名常量（spectral_indices 的 isSentinelSample 收编）；新增声明哨兵感知重载（精确匹配、非有限哨兵禁用该侧、截住 −99 等网值之上的声明哨兵）；AccumulatorStats 契约注释 |
| 2 | **viewshed 环桶按地图单位**（terrain_viewshed.cpp:92 `ceil(d)`）：0.5m 格网轴向偶数格全成孤儿（假不可见楔形）；度单位格网整幅假不可见（中间格不参与遮挡） | 环号归一化为 cell 单位 `ceil(d/minCell)`；前驱不变量（对角内邻对 a,b≥1 格必降环、轴上格由轴向内邻覆盖，双评审独立证明）⇒ counting sort 语义保持、无需桶内排序（首版排序 +43~54% 开销据此移除） |
| 3 | **D8 链不做平地解析**：填洼高原内部全 `dir=0` 汇，火山口汇水面积在溢出口静默截断——推荐的 fill→flow 工作流产出断流河网 | D8 产品在 fill 后 O(8n) 探测平地，有平地才接 `TerrainHydrology::resolveFlats`（与 D∞ 路径同源）；门控避免无平地场景 +81~107% 稳定开销 |
| 4 | **rs:temporal_phenology_multi 漏重复时间戳守卫**（家族内 decompose/smooth 均有）：keep_all 下退化时间轴直达内核，全像素拒绝原因误报 "degenerate_seasonal_component" | 镜像兄弟算子的类型化拒绝（strictly increasing / duplicate_policy=reject / rs:temporal_regularize），输出创建前失败 |
| 5 | **SAR 地形算子无角域校验**：flatten/correction 接受 95°（sin>0 产伪 RTC）与 0°（分母归零“成功”产全幅 NoData），与 backscatter/masks 的既有契约不一致 | 两算子补 (0,90) 类型化拒绝（同族消息契约） |
| 6 | **#1646-5 后半（help 目录防腐烂）**：键名已修但无一致性守卫，“表会静默腐烂” | `catalogKeys()` 访问器 + drift 测试：93 目录键 ↔ 三内置提供者注册名双向一致 |

## 守卫加固（非缺陷修复，防未来误修/腐烂）

- Whittaker n==3/4 **金值 pin**：n==3 对角元正确性依赖 `main[1]`/`main[n-2]` 别名双减（恰得 4λ）——本轨审计曾误判为 5λ 缺陷，主代理手算目标函数证伪后以金值 (20/61,21/61,20/61) 钉死，防止未来“顺手修”引入真 4× 回归。
- 时序链 **E2E**：dated 栈 → rs:temporal_smooth → 6 波段输出（CRS/geotransform/NoData 保持、尖峰抑制、两轮逐字节一致）。

## RED→GREEN 证据（`.planning/zcode-r24-A-science/RED_GREEN.md`）

- RED（master@5fe642ec6 未修代码）：nodata 3/5 用例失败（含 **rs:ndvi 生产路径**负等值对像素 NaN）；viewshed 4/4（亚米格 9/441 可见）；flow_flats 2/2（溢出口 acc≈1 期望 ≥50）；guards/sar 1-2/1-2（未抛类型化错误，35° 对照过证 fixture 有效）。
- GREEN×2：9 新靶两轮全绿；**邻域回归 15 靶两轮全绿**（含 test_terrain_viewshed 1453 断言、test_terrain_foundation5/test_sar_operators/test_temporal_phenology_multi 等既有契约）。

## 性能（`PERF_BASELINE.md`，5 次中位，无墙钟门）

- viewshedR3 1024²：BEFORE 283.4 ms → **AFTER 196.3 ms**（3.70→5.34 Mpix/s；零退化）；RSS 69.3→73.4 MB（dist 数组 double 化）。
- D8 链：无条件 resolveFlats 实测 +81~107% → 不合格故设计为探测门控；无平地场景仅付 O(8n) 扫描（≈1–3% 链成本），有平地场景付解析成本换正确河网。

## 独立评审

- **R1**（另文 `REVIEW_R1.md`）：全新上下文评审员对 8 提交做对抗性复核（数学正确性/各向异性环论证/门控边界/测试判别力），发现闭环后全绿复跑。
- **R2**（另文 `REVIEW_R2.md`）：第二个独立上下文从干净 diff 全量复核 + merge-loss/平台兼容 + R1 修复复核。

## 冲突/完整性门

- `scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`：**PASS**（HEAD 1050 注册 = 基线 1041 + 本轨 9；丢失 0；冲突标记 0；重复 0）。
- `git diff --check` 干净；tests/CMakeLists.txt 仅末尾 append 独立 `BEGIN/END R24-A` 块。
- 兄弟轨写集零交集（src/processing/framework|gdal、src/core、src/agent、pi/ 等均只读；跨轨移交清单见 `CROSS_TRACK_FINDINGS.md` CT-R24A-1..7）。

## 未跑/诚实声明

- 平台：仅 Windows/MSVC Debug 实跑；macOS/Linux 未验证（沿基线状态）。
- `test_d16_temporal_phenology_e2e` 未跑：已知环境性失败 CT-R21-A-1（src/core 对含 8 位数字路径的 `\d{8}` 误判，本 worktree 名含时间戳），与本轨改动无交集。
- 光谱/分类/镶嵌全家族深扫审计因账户限流 NOT_EXECUTED（本轨以 #1687/#1646-6 人工实读覆盖其核心共享面）；SAR 元数据回退、terrain_solar 纬度默认、float32 累积精度等已证候选按 DECISIONS 留痕不凑数。
