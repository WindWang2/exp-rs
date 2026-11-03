# SWARM_REVIEW — exp-rs 全库 swarm 逐行审查 · 2026-11-04

**Track**: master @ `2610cb4741`（上次全库逐行审查 `WHOLE_REPO_REVIEW.md` 钉定 `27b9aa0a63` 之后 **3,126 个 commit、约 +65 万行**——第一方代码几乎翻倍，故本次为全新全量审查而非增量）。
**方法**: 26 个并行审查 agent（swarm）按模块切分单元，六透镜（科学计算契约 / 生命周期与内存 / 并发与状态同步 / 求解器语义 / 契约与元数据漂移 / 错误处理方向）+ 双层深度（逻辑密集文件全文逐行，声明/样板文件结构化通读，每文件必开）；另设回归核验 agent 与独立复核 agent。
**覆盖**: 第一方 `src/` 全模块（~64 万行，42 个模块单元）、vendored QGIS 树全部本地改动（`git diff 27b9aa0a63..HEAD -- src/core src/gui src/ui`，26 文件 +1902/−979 逐 hunk）、`tests/`（47 万行三层可信度审查：全量启发式 + delta 聚焦 + 科学家族深读复算）、`pi/` + `scripts/` + 构建系统、历史发现回归核验。
**复核**: 8 条最高严重度发现经独立 agent 二次核验——**8/8 确认，0 假阳性**（V2/V4/V5 措辞精化，V7 影响面扩大）。

---

## 结论

仓库处于**高审计饱和 + 快速扩张**状态：上次审查的 7 条发现（F-OPS-1..5、F-PI-1/2）**已全部修复**且带行为锚定测试；12 个代表性已闭环 issue 修复经 3,126 commit 后 **零退化**。但扩张带来了 **131 条新发现：1 P0 / 7 P1 / 47 P2 / 76 P3**，集中在三类：**修复未跨表面传播**（#612 度→米换算漏掉 toolbox 算法、staged 发布漏掉两个算法）、**新模块契约缝**（ensemble 成员门禁、vector inspect 形状、planner 数据流）、**fail-open 残留**（QA/审计/验证聚合）。测试可信度显著高于一般大型代码库（delta 新增 5,057 用例零断言体仅 0.8%，科学期望值抽样复算无一钉死错误行为）。

---

## 严重度矩阵

| 模块组 | P0 | P1 | P2 | P3 | 合计 |
|---|---:|---:|---:|---:|---:|
| operators（RT/R1/R2） | 1 | 1 | 5 | 5 | 12 |
| processing（F/A1/A2） | 0 | 2 | 3 | 9 | 14 |
| agent（H/C/M） | 0 | 2 | 12 | 12 | 26 |
| app（S/D/R） | 0 | 0 | 5 | 8 | 13 |
| geospatial / core 第一方 | 0 | 1 | 7 | 9 | 17 |
| data+dataset / experiment+analysis | 0 | 0 | 4 | 9 | 13 |
| sdk+plugins+runtime+python / workflow+cli+contracts | 0 | 0 | 4 | 7 | 11 |
| 长尾（LT1/LT2） | 0 | 0 | 5 | 7 | 12 |
| vendored 本地改动（VD） | 0 | 0 | 1 | 3 | 4 |
| pi/scripts/构建（PSB） | 0 | 1 | 1 | 4 | 6 |
| tests 可信度（TST） | 0 | 0 | 0 | 1 | 1 |
| 回归登记（REG） | 0 | 0 | 0 | 2 | 2 |
| **合计** | **1** | **7** | **47** | **76** | **131** |

---

## P0（1 条）— 已独立复核确认

### OPS-R2-1 · rs:sar_network_inversion 省略可选 displacementOutput 时对空 vector 越界写
- `src/operators/rs/rs_sar_network_inversion_operator.cpp:402-405`（epochPlanes 仅在 `!stackPath.empty()` 时分配）vs `:450-458`（per-pixel 填充 `epochPlanes[e][idx] = ...` 无 stackPath 守卫）；写盘侧 `:487-504` 有守卫，证明守卫意图存在但填充侧遗漏。
- schema required 仅 `{displacementInputs, pairs, velocityOutput}`（:87），最小合法调用 + 任一可解像元（solvedEpochs>0）→ 空 vector `operator[]` UB → 段错误/堆损坏。
- 测试盲区：唯一调用 `tests/test_sar_platform11.cpp:661` 总是传 displacementOutput。
- 修复：填充循环包 `if (!stackPath.empty())`；补 velocity-only 回归测试。

## P1（7 条）— 全部经独立复核确认

### OPS-RT-1 · ensemble 成员的 output.format/uncertainty/多头契约无前置门禁
- `src/operators/runtime/model_ensemble.cpp:323-351`（resolveEnsembleMembers 无成员派生输出校验）+ `tile_inference_engine.cpp:1150-1152`（Probability 哨兵回落到成员自身 format）。
- 复核精化：全体成员声明同一 derived format（如都 labels）时形状检查通过，标签码被 weighted_mean 当概率平均 / vote 产全 0 类（agreement=1.0）——静默垃圾产品原子发布；uncertainty/多头子项为 mid-run 响亮拒绝（迟到而非静默）。

### PROC-F-1 · native_hillshade（工具箱）把地理坐标系 DEM 的度当米传 Horn 核
- `src/processing/providers/qgis_algorithms/algorithms/native/native_hillshade.h:72,128-129`——`rasterUnitsPerPixelX()`（EPSG:4326 ≈ 0.00028 度）直接进 `TerrainAnalysis::hillshade`，梯度放大约 10⁵ 倍，输出静默饱和。#612 修复（`cellSizesMetres`）只覆盖 rs: 算子族，toolbox 表面漏改——同一缺陷类两套修复未传播。

### PROC-A2-1 · resolveFlats 的 epsilon 在 float32 中被吸收——高海拔低起伏 DEM 平地解析静默失效
- `src/processing/algorithms/terrain_hydrology.cpp:192-193,235,260`——epsilon 按 relief 缩放但加法发生在绝对高程量级上：z≈4000 m 时 ulp=2.44e-4，**relief ≲ 128 m 即完全吸收**（如 3800–3900 m 高原面 + 60 m 起伏）。平地仍是严格平地 → D8 dir=0 / D∞ -1 变内部汇，汇流截断、虚假 outlets；`raisedCells` 虚报、头文件"float-exact"契约违约。
- 复核修正：ulp(4000)=2.44e-4（非 4.88e-4）；阈值 relief < 2^e/16（binade 底部）至 z/32。

### AG-H-1 · confirmMapOutput 丢弃 cartography:repair 的修复结果——修复循环数学上不可能收敛
- `src/agent/harness/plan_tools.cpp:1308-1323`——`repairInput`/`preflightInput` 每轮仍喂原始 spec，`repaired.output["mapspec"]` 被丢弃（repair 是纯函数，`cartography_tools.cpp:540` 只经 output 返回）。初始 FAIL 的 spec 无论修复多少轮 verdict 恒 FAIL，最多白跑 3×3 轮。

### AG-C-1 · datasetUnderstandingFromVectorInspect 读错 JSON 形状——矢量数据集理解全部静默 unknown
- `src/agent/contracts/spatial_contracts.cpp:196-201` 从顶层读 `crs/featureCount/geometryType/fields`，而 `vector_inspect_tool.cpp` 这些字段全在 `layers[i]` 内。调用方（grounding_tools.cpp:287、scientific_preflight.cpp:1343）传原始 output，无展平层；raster 分支形状匹配正常，contract 测试只测 raster——矢量事实丢失并写入 ContextLedger 缓存。

### GEO-1 · vectorConvert 几何类型方言不匹配——仅 Point/Polygon/None 能通过
- `src/geospatial/convert/raster_convert.cpp:497-499` 把 `OGRGeometryTypeToName` 输出（"Line String"/"Multi Polygon"/"3D Point"…带空格）传给 `vector_writer.cpp:37-47` 的无空格映射表 → 默认 wkbUnknown → throw。
- 复核扩大：**LineString、Multi*、25D/M、GeometryCollection、"Unknown (any)" 全部 100% 失败**（libgdal strings 运行时取证）；映射表中 "Unknown" 条目是死条目。`io:convert_format` / `io:vector_convert` 的道路网/多部件面转换必然报错。

### PSB-1 · tests/CMakeLists.txt 注册 3 个不存在的 pi 测试文件；现存 6 个套件（含 F-PI-1/2 锚点）无 CI lane
- `tests/CMakeLists.txt:4409/4416/4423` 注册 `prompt_templates` / `remote_sensing_assets` / `scientific_workflow_compiler` 三个 `.test.mjs`——`pi/test/` 中不存在（只有 `..._11` 版本）→ tier1 CI 装 Node 后 `ctest` 必红；反方向 no_drift/bridge_parity 等真实锚定测试无任何 lane 运行。`.planning/r16-d-t08/TEST_MATRIX.md` 已观察到但误诊为环境问题。（主线已独立复核属实。）

---

## P2（47 条）— 按主题分组

### A. 修复未跨表面/跨文件传播（9 条）
- **OPS-RT-3** otb:segmentation/svm 的输出清理只探测字面 `-out`（实际参数是 `-mode.vector.out`/`-io.out`）→ 取消/失败留截断产品（otb_operator_base.cpp:134-140）。
- **OPS-R1-1** rs:rx_anomaly / rs:sam_classify 用普通 `close()`（只 qWarning）而非 `closeWithError()` → flush 失败截断栅格以成功交付；同族其余算子全用 closeWithError。
- **OPS-R1-2** apply_mask / post_classification_change 的 `CPLGetLastErrorType() != CE_None` 终检是死闸（close() 已吞错）且 CE_Warning 误杀好产物（gdal_dataset_wrapper.cpp 机制）。
- **PROC-F-2** band_math / spectral_index 输出无 staged 发布——直接截断旧成果，磁盘满留半成品（同目录 raster_calculator 等三件套已迁移）。
- **PROC-F-3** 12 处矢量算法空 `catch (QgsCsException&) {}` 吞掉变换失败，异 CRS 几何继续参与空间计算（vector_spatial_query.cpp:79-86 等同型）。
- **DAT-2** VRT 生成仍用截断式 QFile 直写，绕过 sidecar 单一写权威（#1457/#1473 纪律漏网，virtual_raster_source_provider.cpp:954-963）。
- **GEO-5** probe 的 `GDAL_HTTP_CONNECT_TIMEOUT` 键名拼错（应为 `GDAL_HTTP_CONNECTTIMEOUT`）→ #809 connect 预算静默失效（probe.cpp:222,243）。
- **GEO-6** 两处 UTF-8 路径窄字符 ifstream（product_adapters.cpp:70、probe.cpp:190）→ Windows 中文目录静默失败（#1178/#1456 同类漏网）。
- **VD-1** qgsziputils zip 硬上限（256MB/条目、512MB 总量、4096 条目）拒开合法大型 .qgz 工程。

### B. 静默科学错误 / 质量门 fail-open（11 条）
- **OPS-RT-2** 二次 acquire 被会话自己的 VRAM 预留卡住——缓存命中失效，重复加载 CPU 会话或错误拒绝（model_runtime.cpp:917-999）。
- **OPS-RT-4** 栅格 ensemble 类词表不一致只在 scene/detection 通道拒绝，栅格通道静默混类（.h 契约声称已拒绝）。
- **CORE-2** TemporalCube 云量 NaN 被 `std::max(0.0, NaN)` 钳为"全晴"→ 被遮蔽像素成 BestPixel 首选（temporal_cube.cpp:440）。
- **CORE-1** TemporalCube::open 不比较 geotransform——同尺寸不同配准场景逐像素错位合成（.h 契约声称拒绝）。
- **DAT-1** 泄漏审计把被关闭的 distance/buffer 检查谎报为"已审计"（leakage_audit.cpp:320-328 vs 523-528）——默认配置即触发。
- **APP-D-1** 打开首选项对话框即静默改写非空工程的 CRS（preferences_dialog.cpp:110-128，注释与代码自相矛盾）。
- **AG-M-2** continuous 分类的 ramp 采样所有档位同一值——连续色带静默退化为单色（style_compiler.cpp:134-149）。
- **AG-M-3** 图表渲染对负值取 fabs——NDVI/dNBR 负值静默画成正值（chart_registry.cpp:692,794,820,841）。
- **LT2-3** 验证 seam 聚合对空产物列表 fail-open 为 "PASS"（零产物=验证通过）。
- **GEO-2** FWHM 未做 WAVELENGTH_UNITS 归一化（WAVELENGTH 修了）→ ENVI µm FWHM 错 1000 倍。
- **GEO-4** 虚拟立方 scatter 丢 BandInfo.dtype → #874 Float32 sentinel 窄化比较被绕过（virtual_cube.cpp:852-856）。

### C. UI/工作流契约漂移（7 条）
- **APP-R-1** 工作流画布 Save/preflight 导出丢步骤 params/gates 等全部不可见字段——重开运行回退默认值，静默科学错误。
- **APP-R-2** 画布与执行引擎零同步：Run 无会话时静默无操作；有会话时执行未修改定义（删掉的步骤照跑）。
- **APP-D-2** populateRasterLayerCombo 存裸 `QgsRasterLayer*`，图层删除后 Fusion/Terrain 对话框 UAF。
- **APP-D-3** ContrastStretch "Custom Photoshop Levels" 导出分段线性角点，Levels 的 min/max/gamma 参数静默丢弃（与预览不一致）。
- **AG-M-4** 主体页 page_if=false 只写无人消费的 `page.visible` 标志——声明的条件静默失效。
- **AG-M-5** atlas sort_expression 不校验——解析失败静默放弃排序，交付页序错乱无诊断。
- **AG-M-6** colorbar 未知 ramp 名静默回退默认绿色渐变；PNG 写失败时条目静默消失。

### D. 并发/生命周期/资源（8 条）
- **AG-H-2** plan_tools 修复账本 `++attempts` 在锁外操作 map 引用——FIFO 逐出后 UAF 写（注释宣称的安全不变量只成立一半）。
- **AG-H-3** artifact_read text 模式 chunk 边界切断 UTF-8 多字节序列时整读被误拒（mcp_server.cpp:1529-1540）。
- **AG-C-2** symbology:apply_raster_ramp 对失效栅格层空指针解引用 + `std::clamp(v,1,0)` UB（bandCount=0）。
- **AG-C-3 / AG-C-4** geometric 配准 / assess_result 对 0 波段栅格缺 GetRasterBand(1) 判空 / 判空顺序颠倒 → nullptr 解引用。
- **AG-C-5** [known #644-部分] vector_inspect extent 的 force=true 回退仍是全文件扫描，违背有界成本契约。
- **WFC-2** 30 分钟硬编码管道超时无任何覆盖手段——长负载（全图推理/SAR 地形校正）永久无法完成。
- **LT2-2** SessionJournal::load 无字节上限整文件读入（"bounded, fail-closed"契约漏网）。

### E. 协议/解析/安全面（8 条）
- **SDK-1** [安全] manifest_version 超出 int 域（如 4294967296）→ jsoncpp LogicError 逃出启动扫描 → std::terminate（plugin_manifest.cpp:932-944，#1038 全称性契约漏网分支）。复核：GUI 启动链全程无 catch。
- **SDK-2** [安全] host-process 注册报告的数组元素无类型检查——恶意 worker 伪造 load 响应击穿隔离契约打崩宿主（plugin_host_process_runtime.cpp:209-257）。
- **WFC-1** 非对象 `params` 被静默替换为空对象——MCP run_workflow 面算子以默认参数执行（CLI/schema 面都拒绝）。
- **LT1-1** 混合数值域下 planner 计划数据流断裂：校准步输出成孤儿、已符合域资产从分析步输入消失，verdict 仍 "feasible"。
- **LT2-1** [P2] preflight verdict="fixable" 但零提案时绕过 allowUnapprovedFixable 策略直接执行。
- **LT2-4** [known-取舍] 教学判定工件指纹为 64KB×8 窗口采样——大工件采样窗之间可被篡改而不被发现（防篡改链目标威胁下保护力不足）。
- **EXP-1** 复现包导入丢失 software_revision——执行指纹自洽门必拒绝（潜伏，生产方暂留空所以 round-trip 测试通过）。
- **EXP-2** benchmark protocol 空 pin 时"可写不可读"，坏一行毒化整个定义列表。

### F. 其他正确性（4 条）
- **GEO-3** inspectRaster 对旋转 geotransform 只取对角两角算 extent（grid_descriptor 四角是对的——同仓漂移）。
- **PSB-2** capability_enrichment.py --check 不校验内容（fail-open 漂移门）且 --strict 文档化但未实现。
- **OPS-R1-6→P2 降级说明**：rs:apply_mask dtype 判定只看波段 1（混合 dtype 栈 Int32/Float64 波段仍经 float32 往返，#612 变体，原报 P3 归入此处仅作索引）。
- **VD/REG 登记项**：REG-1 ISSUES.md 8/10 缺口已填未销账（防重复实现）。

---

## P3（76 条）— 索引

**operators**: OPS-RT-5（ModelCatalog::resolve 不懒加载+头/实现语义相反）；OPS-R1-3（CN 导入定标不排 NoData 哨兵）；OPS-R1-4（change_sam/irmad/log_ratio/cva_angle 跳过网格/辐射态预检）；OPS-R1-5（register_images maxDim 变交付分辨率上限）；OPS-R1-6（apply_mask dtype 只看波段 1）。
**processing**: PROC-A1-2（ISODATA merge 用上一代中心）；PROC-A1-3（PCA 差分一像元非有限整幅 NaN）；PROC-A1-4（SVM 训练后终身持有 O(m²) Gram 矩阵 ~1.4GB）；PROC-A1-5（融合 IHS/GS/linear/brovey 全 NoData"成功"产空产品，与 PCA 分支不一致）；PROC-A1-6（Frost 滤波 ±Inf 邻元毒化输出）；PROC-A1-7（GCP CSV 逗号 id 静默丢点）；PROC-A2-2（同头文件两个 isScaled 形参语义相反）；PROC-A2-3（稀疏解混 Lipschitz 文档与实现相反）；PROC-A2-4（坡度/坡向/山影对四邻全 NoData 有效像元伪造平坦值）。
**agent**: AG-H-4（WAVELENGTH_INCOMPATIBLE 码表重复）；AG-H-5（run_loop 账本按 key 序非 FIFO 逐出+可自逐出重置预算）；AG-H-6（gain_invariance bands:[0,x] 越界 at() 抛异常）；AG-C-6（classification_tool 越界读+JM 哨兵反转+幽灵表面未注册）；AG-C-7（class_count 检查与注释语义相反）；AG-C-8（temporal/terrain 错误码词汇漂移）；AG-C-9（workflow:preflight 路径启发式把 "image/tiff" 当文件误报阻断）；AG-C-10（spectral_inspect 畸形 point 静默回退栅格中心）；AG-M-7（distribute 不计中间条目宽度）；AG-M-8（explain unsat_cores 恒空）；AG-M-9（Preflight/Repair 工具与 RSOperator 条件解析不一致）；AG-M-10（issue 500 上限只对 overlap 两类生效）。
**app**: APP-S-1（Extract Band 菜单内联路径绕过 registry 门控）；APP-S-2（取消竞态下 Completed 结果提交被静默丢弃）；APP-D-4（自定义卷积核输入框死输入）；APP-D-5（时序对话框坐标解析不查 toDouble 失败→0.0）；APP-R-3（OBIA 门禁数段数非类数）；APP-R-4（DOF 钳位 0 与 tooltip 矛盾）；APP-R-5（段选择逆仿射忽略旋转项）；APP-R-6（分类波段非法 token 静默丢弃）。
**geospatial/core**: GEO-7（StacClient 两方法声明未定义）；GEO-8（VSI 前缀表缺 /vsiwebhdfs/ 与 offline_gate 漂移）；GEO-9（STAC GET bbox 6 位精度静默收窄）；GEO-10（range cache 持锁做网络 I/O）；GEO-11（RasterWriter 收 Int64 但无钳位门且自家 Reader 拒读）；CORE-3（PluginHost unloadAll 悬空 instance 窗口）；CORE-4（loadPlugin 无重复加载守卫）；CORE-5（插件目录检查漏文件本身 world-writable）；CORE-6（spatial_split 退化配置 int64 转 UB）。
**data/dataset/experiment/analysis**: DAT-3（PatchGenerator 旋转/退化 geotransform 静默错位）；DAT-4（GroupKFold 空 groupId 坍缩单一伪组）；DAT-5（SampleRecord kind↔payload 失配 toJson 抛 bad_variant_access）；DAT-6（GovernanceStore schema 门读失败按旧库放行可降版本戳）；DAT-7（ArtifactStore 缺 #1449 生命周期锁）；EXP-3（跨种子排序缺 classCode 违反确定性声明）；EXP-4（ignoreLabels 只作用 truth 侧）；EXP-5（Failed 事件证据构造后丢弃）；EXP-6（KMeans 未映射簇精度路径回退 0 与 Apply 回退 unclassified 不一致）。
**sdk/workflow/cli**: SDK-3（SIGCHLD-SIG_IGN 下退出码误报 0——失败工具被放行发布）；WFC-3（data 命令族不拒未知旗标+末位值旗标静默丢）；WFC-4（lab --json 与全局信封 --json 同名冲突）；WFC-5（dataset/experiment 解析器值旗标吞全局旗标）；WFC-6（环境变量占位符两套大小写语义）；WFC-7（--no-execution-cache 在子命令模式不可用）；WFC-8（workflow run/validate 读不同 inputs 形状——引擎形状接线被 run 静默忽略）。
**长尾**: LT1-2（计划 JSON 自引用输入绕过无环校验）；LT1-3（resolver band 配对口径不一致）；LT1-4（operator_catalog 读取无上限）；LT1-5（grader linearWindow 契约注释漂移）；LT1-6（faultlab 容差相对/绝对语义漂移）；LT2-5（fake_seams 空 vector size()-1 下溢 UB）；LT2-6（GuidanceStore 读取无界）。
**vendored/pi/build/tests**: VD-2（zip 循环未查 zip_stat_index 返回值——加固路径读未初始化栈值）；VD-3（unregisterExternalRenderJob 切断全部 destroyed 连接）；VD-4（settle 语义仅对 ParallelJob 验证）；PSB-3（resource_guard monotonic 跨重启死锁判定）；PSB-4（compiler_11 测试 canary 吞失败为 skip）；PSB-5（benchmark harness readline 不受 deadline 约束）；PSB-6（fetch_boost_headers 零完整性校验）；TST-1（恒真断言伪装 #784 覆盖）；REG-1（ISSUES.md 过时）；REG-2（F-OPS-1 校验重复两遍）。

---

## 回归核验（全部通过）

- **上次 7 条发现**：F-OPS-1（commit 4e7b6a55f9）、F-OPS-2（9ede2eec66）、F-OPS-3 fail-closed（d3418d7fce）、F-OPS-4（0f5743e4a0）、F-OPS-5 桶化 NMS+取消探针（c5ce086da3）、F-PI-1 failDesyncedStream+拆流（297d5b0c36）、F-PI-2 去分叉+no_drift 测试钉死——**全部在位修复**。
- **12 个代表性闭环修复**（#694/#756/#774/#791/#807/#811/#825/#863/#896/#925/#926/#928/#945/#801/#856）：**全部完好，零退化**。
- **ISSUES.md**：10 条缺口断言中 8 条已被新算子填掉（T-1/T-2/T-3、S-2、H-1/H-2/H-3、C-2），登记过时；S-1（SAR 多时相日期）与 C-1（制图非管道算子）仍真实。

## 测试可信度

delta 新增 626 个测试文件 / 5,057 个 TEST_CASE；零断言体候选 40 个全部核实为 helper 委托实断言；SKIP 20 处全部为环境条件或文档化 opt-in；`[!shouldfail]`/`[!mayfail]` 零使用；15+ 个科学期望值手工复算全部正确（ensemble 方差、6S 反演、DOS2、Whittaker 金标、MK/Sen 等）。唯一实质问题：TST-1 恒真断言（P3）与 PSB-1 的注册漂移（pi 测试注册缺 CI 接线）。主要残余风险：343 处 sleep 轮询构成潜在 flaky 面（均有界、超时诚实失败）。

## 覆盖诚实声明

- 第一方 `src/`：42 个模块单元全覆盖，逻辑密集文件全文逐行精读，声明/样板结构化通读；各单元覆盖声明见审查过程记录（精读约 60-70%，结构过约 30-40%，跳过 0——唯一例外为按指令跳过的 vendored qgs* 文件与 .ui XML）。
- vendored QGIS（src/core、src/gui、src/ui 约 141 万行）：按仓库既定口径排除上游基线审查；**全部本地改动**（26 文件）逐 hunk 审查（VD 系列）；src/core/gui 内第一方新文件（plugin_host、temporal_cube、radiometric_state、spatial_split、antigravity_*）全量逐行（CORE-X 系列）。
- `src/ui` 551 个 .ui XML：随 app 单元做结构检查，未逐行。
- tests/ 47 万行：三层方法（非逐行）；深读约 40 个文件 + 全量启发式 + delta 机器扫描。

## 建议处置顺序

1. **立即**：OPS-R2-1（P0，一行守卫 + 回归测试）；SDK-1/SDK-2（启动即崩/隔离击穿，改动小）。
2. **本周**：7 条 P1——各带明确修复草图；GEO-1（映射表补别名，影响面最大的功能性 P1）；PSB-1（CI 必红，改注册块）。
3. **排期**：P2 按主题分组批量处理——A 组（修复传播，9 条）收益最高，多为同款模式套用；B 组 fail-open 需逐条定语义方向。
4. **维护**：REG-1 ISSUES.md 销账；测试 sleep 轮询面与 pi CI 接线补齐。
