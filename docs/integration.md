# Integration — Scientific Data Passport ↔ other tracks

本文件是 RS14-01 科学状态层与并发方向 / 未来消费者的**唯一接线清单**。
所有接线点均为建议的最小 delta,不要求对方 track 依赖本方向内部类型。

## 1. Agent/MCP 只读工具(推迟的接线点)

现状:`passport` 通过 CLI(`sicnu_geo_rs_cli passport --path <file> --json`)与库 API
(`resolveAssetState`)提供 machine-readable 查询。MCP 进程内工具**有意推迟**:
注册一个 data tool 需要同时改 `src/agent/mcp_server.cpp`(tools/call 分发)+
`mcp_server.h`(handler 声明)+ 一个 `ToolProvider` 定义——前两者是 20 个并发
track 的高冲突中央文件,而 capability 门禁(completeness/drift)只覆盖 `rs:`
算子,不含 data 工具,风险收益不成比例。

未来接线(对齐 `data:get_lineage` 的既有模式):
1. `src/agent/tool_catalog/data_tool_provider.cpp` 增加 `makeAssetPassportTool()`
   (input schema:`{"asset_id": string}`;family `data`;name `data:asset_passport`)。
2. `mcp_server.cpp` tools/call 分发增加 `data:asset_passport` 分支,handler:
   - `DataManager::asset(id)` → `makeCatalogFacts(snapshot)`;
   - snapshot 的 `source().canonicalSource` → `collectDatasetFacts(path)`;
   - `resolveAssetState(input)` → `assetStateToJson` → bounded QVariantMap。
3. surface parity 自动成立:catalog provider 的工具经 UNION 投影进入
   MCP tools/list、CLI tools、get_tool_schema 三个表面。

## 2. 教学 UI 面板(GUI 接线点)

`renderTeachingSummary(state)` 是 Qt-free 视图模型;面板侧只需要:
- 参照 `src/app/panels/asset_catalog_index.{h,cpp}` 的资产选中信号;
- 对选中资产调用 CLI 等价的链路(collect→resolve);
- 用 `RsResultSummary`(`src/app/widgets/rs_result_summary.h`)的
  `setResult(Json::Value)` 模式渲染五个证据桶;或直接渲染
  `teachingSummaryToPlainText` 的文本。
不新增业务逻辑——所有科学语义都留在 core。

## 3. RS14-09 Task Planner

Planner 在生成 ScientificPlan 前可用护照做前置条件检查:
- 读 `claims[path].kind`:仅当 `known` 才可作为硬前置;`assumed` 需要用户确认;
  `unknown` 进 plan 的开放问题;`conflicted` 必须显式阻塞。
- 库 API 即接口:`resolveAssetState` + `claimFor`。

## 4. RS14-10 Unified Scientific Verifier

验证器可以把护照作为**输入证据**而不是重新采集:
- `sicnu.asset_state.v1` 是确定性 JSON,可进入 verifier 的证据集;
- `radiometric.unit` + `domain` + `numeric_scale` 支持"两幅影像辐射可比"类检查;
- 不要求 verifier 依赖 core 库——直接消费 JSON 即可。

## 5. RS14-17 Reproducibility Capsule

护照可导出进 capsule(只读工件):
- `serializeState` 字节确定 ⇒ capsule 内 diff 稳定;
- schema id 已含版本,迁移策略与 capsule 的 versioning 对齐。

## 6. RS14-08 Capability State Graph

无共享类型。能力图以 operator 为键,护照以 asset 为键;两者在
"agent 决策前检查"场景相遇(能力图回答"能否做",护照回答"数据现在是什么")。

## 7. 与现有事实源的关系(单一事实源声明)

| 事实 | 权威 | 护照角色 |
|---|---|---|
| 资产身份/结构 | `CatalogRecordStore` / `AssetSnapshot` | 投影(source tag `catalog:*`) |
| 文件元数据 | GDAL `SICNU_*` 键 | 投影(source tag `gdal:*`) |
| 传感器波段轴 | `data/products/sensor_profiles/*.json` | 投影(inferred claims) |
| 谱系 | `DerivationRecord` | 投影(`provenance` section) |
| 模型 sidecar | `<model>.meta.json` | 投影(`model_derived` section) |

护照不缓存、不写回、不替代上述任何来源;解析永远按需进行,O(bands + facts)。
