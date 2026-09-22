## RS14-16: Scene/Dataset Suitability Assessor 数据适用性评估器

Closes the RS14-16 track goal: answer "does this dataset / scene set fit this experiment goal?" **before** any compute is spent, with a typed, versioned, machine-readable report — no simple bools.

### 目标与动机
实验或 Agent 规划前，需要结构化判断数据适用性。现有能力（DatasetQaReport、composition、grid compat、STAC 元数据）各自回答单点问题；本 PR 增加把**目标需求**与**数据事实**对接的评估层：本科教学视角给出"为什么这组影像不适合本实验"的解释 + 缺口清单；Agent 视角给出可持久化、可回放、可验证的 JSON 报告。

### 架构（新纯科学核心叶子 `sicnu_suitability`，namespace `sicnu::suitability`）
- **四态 lattice** `SuitabilityLevel{Suitable, Marginal, Unsuitable, Unknown}`，严重度秩 Unknown > Suitable（partial evidence never Suitable，fail-closed，对齐 DatasetQaReport 既有姿态）；not-applicable 的 criterion 不参与聚合但列在报告中；缺元数据永远 Unknown，绝不降级为 N/A。
- **11 个 criteria**（稳定 id）：`spatial.coverage/resolution`、`temporal.coverage/density/seasonality`、`quality.cloud`、`spectral.bands`、`labels.availability`、`grid.compatibility`、`model.compatibility`、`uncertainty.sources`。每条：level + summary + evidence（measured vs required）+ notes（诚实声明没查什么）+ typed gaps（如 `band.missing.nir`、`season.missing.winter`）。
- **单一事实源**：网格判断只调 `sicnu::data::compareGrids`；任务族只复用 `sicnu::dataset::BenchmarkTaskFamily`；错误词汇只用 `Result`/`Diagnostic`；不发明第二套 Registry/provenance/store。
- **8 个 task profiles**（classification/segmentation/change_detection/object_detection/regression/temporal_prediction/spectral_matching + phenology 附加 profile）：只填默认值，goal 显式值永远覆盖；`phenology` 要求全年四季（缺冬 → `season.missing.winter` Unsuitable）。
- **provider abstraction**（规模查询唯一通道）：`SuitabilityDataProvider` + `InMemoryDataProvider`（fake/TDD）+ `StoreDataProvider`（DatasetStore **只读**投影，caps：1000 scenes / 200 grid pairs / 50000 rows / 64 facet values；截断显式上报为 `factsTruncated` → labels 判定降格 + uncertainty source——沉默截断是 bug）。DatasetStore 写路径零修改。
- **诚实性设计**：GSD 契约为米制，不知道就是空（绝不拿 CRS 单位像素尺寸冒充）；CRS 不可比 → 排除该场景 + note，全排除 → Unknown（核心不重投影）；NaN/inf/超界输入 → typed 诊断，不 clamp 不崩；JSON 整数解析有界门（无 double→qint64 UB）。
- **teaching 模式**：`teachingExplanation(report)` 逐 criterion 人读叙事，与 JSON level/gap 语义一致性有测试锁定。
- **agent 工具**：`suitability:assess` / `suitability:profiles`。适配器在 `sicnu::suitability::agent_adapter`（无需链 agent 库即可单测）；`data_platform_tools.cpp` 仅薄壳（2 defs + 1 prefix + 2 dispatch）。契约与 `dataset:` 先例一致：结构坏请求 throw（MCP isError），内容无效软失败 `{valid:false, diagnostics}`，成功返回完整 report JSON + digest（**可调用且可验证**：`fromJson + contentDigest` 复核）。capability mirror / `kDataPlatformPrefixes` 刻意不动（#1151/#1187 避让区；integration.md 写明纳管前提）。

### 文件清单
- 新模块：`src/suitability/`（24 对头/源：types/report/goal/profiles/scene_candidate/dataset_facts/provider/store_data_provider/6 个 criteria/assessor/teaching/seasonality/agent_adapter）+ `CMakeLists.txt`（Qt-only 静态库，science-core 层 guard 同 sicnu_dataset：禁 GUI/network）。
- 中央 delta（最小）：顶层 `CMakeLists.txt` +3 行（add_subdirectory）、`src/agent/CMakeLists.txt` +1 行、`src/agent/data_platform_tools.cpp` 薄壳、`tests/CMakeLists.txt` 尾部追加、`.gitignore` planning 白名单 3 行。
- 测试：`tests/test_suitability_{core,spatial,spectral,temporal,labels,store_provider,profiles,adversarial,uncertainty,teaching,agent_tools}.cpp` — 11 套件，**1385 assertions / 111 cases 全绿**（store_provider 用真实临时文件 DatasetStore）。
- 规划：`.planning/RS14-16-scene-suitability/{recon,plan,slices,progress,integration}.md`（含全部锁定语义决策记录与 review gate 记录）。

### 测试证据
- 全部 11 套件绿（见上）；RED→GREEN 纪律逐 slice 执行；对抗套件含 missing-metadata 矩阵、NaN/1e300/inf 输入、截断可见性、双跑逐字节确定性重放、grid 抽样 vs 暴力全比对拍。
- 变异验证（独立 reviewer 执行，5/5 杀红后复绿）：overall 聚合、CRS 排除→Unknown、截断降级、grid 抽样确定性、teaching 一致性。
- 逐字节确定性：同输入两次 assess 的 JSON 与 SHA-256 digest 完全相同（QHash 序列化前排序）。

### 性能/资源证据
- 1000 场景 + 64-class facts + digest + teaching 实测 **7.7 ms**/次（预算 50 ms，-O2）；坐标压缩矩形并集最坏 O(n² log n)；caps 上界显式；纯内存无 I/O；4 线程并发 assess digest 一致。
- 构建全程 `CMAKE_BUILD_PARALLEL_LEVEL=1` / `-j 1`，零新增 compiler warning。

### 已知限制（诚实声明）
- 评估核心不做重投影/重新采样：CRS 不可比即 Unknown + note（QGIS/GDAL 侧可在投影层先预处理）。
- 北半球气象季假设（seasonality.h 头注释声明；goal 尚无半球字段）。
- 空间覆盖基于矩形 footprint（无多边形几何投影）。
- temporal.density 的 facts 时间范围最多贡献 1 个估计"时间簇"（evidence + note 说明估计方式）。
- goal 的 bool/零值字段无法显式表达"覆盖 profile 默认为无要求"（goal JSON v1 无三态；注释与测试锁定，未来 schema v2 可加）。

### 与 open issues 的去重结果
不触碰 #1146–#1187 任何避让区（SAR/domain/mission/workflow/plugin/TaskCenter/catalog 一致性/jsoncpp 深度炸弹等）。#1184（agent 10k 截断）：本模块 cap 体系独立且显式上报，未动该 issue 面。#1151/#1187（capability mirror 红灯）：`suitability:` 前缀刻意未加入 `kDataPlatformPrefixes`/knowledge，integration.md 写明修复后纳管步骤；`test_capability_drift` 红点集合与 master 一致（observed，不新增）。

### 与其他 19 tracks 的边界（动态去重 + union merge 2026-09-22）
首轮去重：与当时全部 10 个 open RS14 PR（#1194–#1203）分支逐一 diff，对 `src/suitability/`、`data_platform_tools.cpp` 零文件重叠。提 PR 前复检：master 已前移至 `14bef28949`（46 commits，#1194–#1235 等已合并）→ 执行**语义 union merge**（`.gitignore` 两个 track 白名单块并集、`tests/CMakeLists.txt` 双方追加块并集；agent 侧薄壳经自动合并后逐项核验完好），合并后 11 个 suitability 套件全绿、surface 套件复跑（结果见下）。master 新增内容中无任何 suitability 实现重复（`git grep suitability origin/master -- src` 仅 capability_graph.cpp 一处无关文案）。消费兄弟 track 产物的接线点见 `integration.md`，均为未来 adapter 位，不复制对方实现。

### Rollback
纯新增模块 + 5 处最小中央 delta；回滚 = revert 本 PR。

🤖 Generated with [ZCode](https://github.com/zhipu-ai/zcode)
