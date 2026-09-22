## RS14-01: Scientific Data Passport — RemoteSensingAssetState 统一科学状态层

### 目标

为所有遥感资产建立**可版本化、可查询、可解释、可验证**的统一科学状态对象(schema `sicnu.asset_state.v1`),作为本科实验教学与 AI Agent 的共同"世界状态"。核心是一层**纯只读投影**:把分散在 catalog 快照、GDAL `SICNU_*` 元数据、sensor profile 注册表、DerivationRecord 与模型 sidecar 中的既有事实聚合为单一对象,并为每个字段标注证据类型 `known / inferred / assumed / unknown / conflicted`——缺失有意义、假设有记录、冲突不裁决。

### 架构

```
facts (来源标签化的 DTO)          纯投影(resolveAssetState)         消费表面
┌────────────────────────┐   ┌────────────────────────────┐   ┌────────────────┐
│ DatasetFacts (GDAL)    │   │ · 辐射词表归一(3 套词表合一)│   │ CLI `passport` │
│ CatalogFacts (Qt)      │ ─▶│ · 证据格(声明优先/家族次之)│ ─▶│ 教学视图(文本) │
│ SensorProfileFacts     │   │ · 冲突检测 + confidence 格 │   │ JSON v1 文档   │
│ DerivationFacts (Qt)   │   │ ▼ RemoteSensingAssetState  │   │ 库 API / diff  │
│ SidecarFacts (JSON)    │   └────────────────────────────┘   └────────────────┘
└────────────────────────┘
```

- **核心库 `sicnu_scientific_state`**:仅 jsoncpp,无 Qt/GDAL——在最轻的 `sicnu_add_sdk_test` lane 上测试。
- **`sicnu_scientific_state_gdal`**:一次只读打开 + 元数据查询,绝不扫像素;modern-GDAL const API。
- **`sicnu_scientific_state_catalog`**(C++20,Qt):`AssetSnapshot`/`DerivationRecord` → 事实。
- **CLI `passport`**:`--json`(机器)、`--teaching`(学生五桶视图)、`--diff <passport.json>`(before/after,`sicnu.asset_state_diff.v1`)。
- 教学视图是 Qt-free 视图模型,GUI 面板接线点写在 `docs/integration.md`(本 PR 不动 GUI)。

### 文件清单

新增 `src/scientific_state/`(核心 8 文件 + `gdal/` + `catalog/`)、CLI `cli_passport_commands.{h,cpp}`、测试 10 个 target、`docs/scientific-state/{overview,schema}.md + examples/*.json`(CLI 真实输出)、`docs/integration.md`、`.planning/rs14-01-scientific-data-passport/`(recon/plan/slices/progress,`git add -f` 入库)。对既有文件的 delta:**根 CMakeLists +5 行、cli_commands.cpp +7 行、cli/CMakeLists +3 行、tests/CMakeLists 追加注册块**。共 45 文件,约 +8.8k 行。

### 测试证据

10 个 target、**616 断言 / 107 用例全绿**(sdk lane 7 个 + io lane 1 个 + Qt lane 1 个 + review oracle 1 个):

| target | lane | 断言/用例 |
|---|---|---|
| core(schema round-trip/确定性/typed 拒绝) | sdk(仅 jsoncpp) | 76/12 |
| resolver(证据格/辐射词表/波长归一) | sdk | 78/18 |
| geo(几何/validity/temporal) | sdk | 96/22 |
| provenance(谱系/sidecar 解析) | sdk | 71/15 |
| diff(before/after 契约) | sdk | 41/12 |
| teaching(五桶/agent 一致性) | sdk | 31/8 |
| fixtures(5 资产家族契约) | sdk | 102/9 |
| review(审查 oracle) | sdk | 21/5 |
| gdal(合成 GTiff 端到端) | io(GDAL) | 58/3 |
| catalog(真实 DataManager) | Qt | 42/3 |

杀伤力由独立对抗审查以 **8 处变异测试**验证(7 杀 1 存活,存活者为冗余双排序、行为仍被钉住);P1 修复后的关键负路径(超深 JSON、错型 enum 字段)有专门 oracle 套件。TDD 全程 RED→GREEN:每个 slice 的测试先于实现并被证明失败(详见 progress.md)。

### 性能/资源证据

- 解析按需进行、无缓存/无持久化;复杂度 O(bands + facts + claims)(confidence 经 path→kind 索引,无 bands×claims 项——审查实测 4096 波段病态输入 757ms 后已修复为线性)。
- 上界:bands 4096、temporal refs 256、metadata items 512/scope;溢出一律截断 + 显式 note(`bands.truncated` / `temporal.truncated` / `facts.metadata_truncated`),绝不静默。
- 全程 `ninja -j1`(默认单线程),仅窄 target 增量构建;从未全量 clean build。

### 已知限制

1. **MCP agent 工具有意推迟**:接线需改 `mcp_server.{h,cpp}`(20-track 高冲突中央文件);machine-readable 表面由 CLI `--json` + 库 API 提供,接线步骤已写入 `docs/integration.md`。
2. sensor profile 推理通道目前仅测试 fixture 在用;真实 `sensor_profiles/*.json` 加载器的适配器是文档化的休眠接缝。
3. `--json` 与 `--teaching` 同给时 JSON 优先(usage 已注明)。
4. 浮点序列化跟随 jsoncpp 默认精度("3 位小数"的 confidence 约束在数值层面成立,非字节层面)。
5. 审查两轮后仍遗留的 P3:teaching 行只显示排序后首个 source;CLI teaching 输出直写 stdout(非 CliIO)。均已记录 progress.md,不阻塞。

### 与 open issues 的去重结果

未触碰任何已知问题区域:SAR #1146/#1147/#1164/#1165(本 track 只把 `SICNU_SAR_STATE_ASSUMED`、SAR dual-key conflict、domain **原样投影**为 claims,不改任何 SAR 行为);#1148/#1149 mission;#1152/#1158 workflow;#1153 ImportCenter;#1154/#1155 jsoncpp bomb(本 track 新解析面遵循 stackLimit=128 纪律,不回修既有 O4 点);#1151/#1187 capability mirror(本 track 是 asset 键的新 id 空间,未动 mirror);catalog/experiment 一致性组(#1161/#1171–#1173/#1184);#1180 UAF;#1185 CLI 并发(本 track 无 std::async)。

### 与其他并发 track 的边界

Re-check 于 PR 创建时:master 仍在 `4f6632e1f`(无需 rebase/union merge)。Open PRs #1188–#1194:
- **#1194 RS14-03 Repair Planner / #1193 RS14-09 Task Planner**:消费状态做规划;本 track 明确"不做 plan/repair",提供 `resolveAssetState` + `claimFor` 只读查询。互补,无代码重叠。
- **#1191 RS14-10 Verifier**:可消费 `sicnu.asset_state.v1` JSON 作为证据输入(integration.md §4)。
- **#1188 RS14-17 Capsule**:护照字节确定,可作 capsule 导出工件(integration.md §5)。
- **#1192 RS14-08 Capability Graph**(operator 键 vs asset 键)、**#1190 Curriculum、#1189 Benchmark**:无共享类型;接线点在 `docs/integration.md`。

### Review Gate

两轮独立对抗审查(第一轮:发现 2 P1 + 5 P2,8 变异杀伤力验证;第二轮:逐项实证修复 + 补充扫描,发现 0 P0/P1/P2、4 P3 并当场修复)。最终 verdict:**可进 PR**。完整审查记录与逐 slice RED/GREEN 证据见 `.planning/rs14-01-scientific-data-passport/progress.md`。
