# Scientific Data Passport — 统一科学状态层(RS14-01)

## 这是什么

`RemoteSensingAssetState`(schema `sicnu.asset_state.v1`)是任一遥感资产的**可版本化、可查询、可解释、可验证**的统一科学状态投影。它把分散在三个表面的既有事实——catalog 快照(`AssetSnapshot`)、文件级 GDAL `SICNU_*` 元数据、各类 sidecar 与 provenance——聚合为一个**只读**对象,并为每个字段标注证据类型:

| 证据类型 | 含义 | 示例 |
|---|---|---|
| `known` | 由权威来源显式声明 | GDAL `SICNU_RADIOMETRIC_STATE=surface_reflectance` |
| `inferred` | 由解析器从另一个声明事实推出 | 波段角色来自 sensor profile 波段轴 |
| `assumed` | 应用了有文档记载的系统默认值 | 光学 FSM 默认:缺失标记 ⇒ `digital_number` |
| `unknown` | 无来源也无默认;缺失本身有意义 | 未声明的传感器 modality |
| `conflicted` | 两个来源互相矛盾;两个值都保留,绝不静默裁决 | `SICNU_SAR_CALIBRATION` 与 `SICNU_RADIOMETRIC_STATE` 不一致 |

**它不是**:第二套 registry、第二套 provenance、metadata 的替代品、或任何会写回数据的东西。它是纯投影——没有 I/O、没有缓存、没有持久化;相同输入永远产生字节级相同的输出。

## 模块布局

```
src/scientific_state/            核心库 sicnu_scientific_state(仅 jsoncpp,无 Qt/GDAL)
  asset_state_types.h              值类型 + 证据格(lattice)
  asset_state_json.{h,cpp}         确定性序列化 sicnu.asset_state.v1 + 类型化拒绝
  state_facts.h                    带来源标签的事实 DTO(适配器输出)
  asset_state_resolver.{h,cpp}     事实 → 状态纯函数(词汇归一、冲突检测、confidence)
  asset_state_diff.{h,cpp}         before/after 状态差异 sicnu.asset_state_diff.v1
  model_sidecar.{h,cpp}            分类器 sidecar(.meta.json v1/v2)解析(stackLimit=128)
  teaching_view.{h,cpp}            学生视图模型(Qt-free,五个证据桶)
src/scientific_state/gdal/       sicnu_scientific_state_gdal(GDAL,仍无 Qt)
  gdal_state_facts.{h,cpp}         一次只读打开 → DatasetFacts(绝不扫像素)
src/scientific_state/catalog/    sicnu_scientific_state_catalog(Qt)
  catalog_state_facts.{h,cpp}      AssetSnapshot / DerivationRecord → 事实
```

## 两个消费表面

- **学生(教学)**:`renderTeachingSummary` 把护照渲染成五个桶(已知/推断/假定/矛盾/缺失),每行保留字段路径,便于和 JSON 对照。CLI `passport --teaching` 直接输出;GUI 面板接线点见 `docs/integration.md`。
- **Agent(机器)**:`passport --json` 输出 `sicnu.asset_state.v1` 规范文档(字节确定、可 round-trip、可 diff);库 API(`resolveAssetState`)供进程内查询。**不做 plan/repair**——那是其他 track 的职责。

## 科学术语红线

- 辐射状态词表归一只做投影:FSM 大写词表、产品小写词表、SAR 标定 token(sigma0/gamma0/beta0)在 `SICNU_RADIOMETRIC_STATE` 上归一;`SICNU_SAR_DOMAIN` 原样记录,**绝不解释 dB/linear**(SAR 科学正确性问题属于专门修复任务)。
- `SICNU_SAR_STATE_ASSUMED`(如 `sigma0_legacy_undeclared`)投影为 `assumed` claim,note 保留原始 token。
- 光学"缺失标记 ⇒ DN"是 `src/core/radiometric_state.h` 的文档化默认;护照把它变成**显式假设**(note `radiometric.fsm_default`),不改变 FSM 行为。

## 测试

| 目标 | lane | 断言/用例 |
|---|---|---|
| test_scientific_state_core | sdk(仅 jsoncpp) | 76 / 12 |
| test_scientific_state_resolver | sdk | 78 / 18 |
| test_scientific_state_geo | sdk | 96 / 22 |
| test_scientific_state_provenance | sdk | 71 / 15 |
| test_scientific_state_diff | sdk | 41 / 12 |
| test_scientific_state_teaching | sdk | 31 / 8 |
| test_scientific_state_fixtures | sdk | 102 / 9 |
| test_scientific_state_gdal | io(GDAL) | 58 / 3 |
| test_scientific_state_catalog | Qt(全量) | 42 / 3 |

sdk/io lane 均 Qt-free;fixture 运行时合成(仓库惯例,无 committed geodata)。

## 文档

- `docs/scientific-state/schema.md` — 字段参考、词表、confidence 公式
- `docs/scientific-state/examples/*.json` — CLI 真实输出
- `docs/integration.md` — 与其他 19 个 track 的接线点
