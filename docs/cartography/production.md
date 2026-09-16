# Cartography Production 11.0 — produce chain, manifests, atlas & series

平台 10.0 让 `cartography:compose/preflight/validate/repair/export` 五算子在
agent/CLI/GUI 四路同引擎可达。11.0 在**不新增第二套引擎**的前提下补齐生产侧：
一条编排链（produce）、持久导出证据（manifest）、图集逐要素交付（atlas
delivery）、序列物化（series planner）与模板治理（governance）。

## `cartography:produce` — 一次调用的生产链

`produce` 是编排而非引擎：按序调用既有自由函数（validate → 升级 → 条件/构
图解析 → 编译 → preflight → 有界 repair → 导出 → manifest），新增的只有三件
引擎从未提供的事——

1. **一次调用 = 一个交付目录**。产物与清单一起原子落盘；失败/取消时回滚，
   目录保持调用前内容（无半成品）。
2. **进度 + 协作取消**贯穿每个阶段与图集每一页边界（TaskCenter 作为唯一执
   行器；GUI dock 经同一注册路径提交）。
3. **诚实的结果封套**：质量裁决、修复台账、digest、清单路径。

入参：`{mapspec, directory, format?, file_name?, dpi?, write_manifest?,
require_preflight_pass?, max_repair_iterations?}`；`format` png|pdf|svg，
`dpi` 72..1200，`max_repair_iterations` 钳制 1..10（与 repair 工具一致）。

类型化拒绝码（`error_code`）：`INVALID_PARAMETER` / `VALIDATION_FAILED` /
`COMPILE_FAILED` / `PREFLIGHT_NOT_PASSED`（仅 `require_preflight_pass` 开启
时有界修复后仍不过时）/ `EXPORT_FAILED` / `MANIFEST_FAILED` /
`UNSAFE_LEGEND_AUTO_UPDATE`（声明的自动更新图例镜像空图层集——本 QGIS 构建
在该配置下 `QgsLayoutItemLegend::paint` 会无限循环；修复：给地图/项目加图
层，或声明 `columns > 1`）/ `PRODUCE_CANCELLED`。

三个 surface 完全同引擎：

- **agent tool** `cartography:produce`（cartography_tools.cpp）；
- **RSOperator** `cartography:produce`（workflow 节点 / CLI pipeline /
  TaskCenter / MCP）；
- **GUI** 制图工作台「生产导出」按钮（TaskCenter 后台任务 + 停止按钮）。

## 导出清单（export manifest）

每次交付在产物旁写入 `<base>.<format>.manifest.json`（原子 tmp+rename）：

```json
{ "kind": "cartography_export_manifest", "manifest_version": 1,
  "layout_name": "...", "format": "png", "dpi": 300, "page_count": 3,
  "provenance": { "template": "...", "template_provenance": {...},
                  "components": [...], "bindings": [...] },
  "structural_digest": "<compose 时几何 SHA-256>",
  "pages": [ { "file_name": "city_1.png", "sha256": "...", "bytes": 12345,
               "feature_id": "1", "label": "city_1",
               "extent": {"x_min":..,"y_min":..,"x_max":..,"y_max":..,"crs":"EPSG:4326"} } ],
  "manifest_digest": "<payload SHA-256>",
  "environment": { "qt_runtime": "6.8.0", "qgis_version": "...", "os": "...",
                   "font_substitutions": [...] } }
```

- `manifest_digest` 覆盖 payload 全部成员（canonical 序列化）；**环境段不进
  digest**——两台主机对同一交付的环境描述可以不同，字节相同的交付不能因此
  看起来不同。
- `pages[]` 每条可独立复核：对磁盘文件重算 SHA-256 与 `bytes` 比对即可。
- 绑定溯源（WP-E）：`provenance.bindings` 记录声明了 `binding` 的条目
  （id/collection/mode/layer/field/expression，≤64 条）。

## 图集逐要素交付（atlas delivery）

`page.atlas` 的配置/校验平台 5.0 已有；11.0 补上**执行**——
`exportMapAtlas`（export.cpp，单一导出权威）用 `QgsLayoutAtlas` 的
beginRender/next 迭代，逐页 temp→校验(sha256)→rename 原子写入：

- 文件名 = `<base>_<filename_expression 求值结果净化>.png`（非法/重复字符
  折叠为 `_`；空结果回退 `p<N>`；大小写不敏感去重）。
- 每页记录 feature_id / label / 几何包围盒（coverage 图层 CRS + authid）。
- 每页边界可取消；运行失控守卫 `kMaxAtlasExportPages=512`（超出为类型化拒
  绝，回滚全部已交付页）。
- **png only**：pdf/svg 在本 QGIS 构建只能输出"全部页拼一个静态文档"——
  对 atlas 版式的 pdf/svg 请求 behaves exactly like `cartography:export`
  （清单记 `mode:"single"`），绝不静默伪装成逐要素交付。
- atlas 版式未启用 coverage layer = 类型化拒绝；从不静默退化为单页。

## 序列物化（series planner）

`src/agent/mapspec/series_planner.*`：单页模板 + 序列定义 → 多页 MapSpec
（MapSpec **v6**）。

- 两种来源：`table`（显式行：region/time 表）与 `vector`（coverage 图层，
  filter/sort/字段变量/几何包围盒）。
- 每行一页：页级 `variables`（≤32 标量）、`series_row` 溯源（index/
  feature_id/title）、`crs` 溯源标签；行 extent 落在该页 map frame。
- 替换记号（计划期替换，先于编译，与 QGIS `[% %]` 表达式语法无冲突）：
  `{{name}}`、`{{page_number}}`、`{{page_total}}`；未知记号保留原样并记入
  problems（诚实台账）。
- 页 0 保留模板 id；页 k 克隆为 `<id>-p<k>` 并重映射 map_ref / locator.target
  / constraints[].items 引用。
- 可选索引页（`index_page.enabled`）：`page.role:"index"` + 汇总 label。
- 硬界：物化 ≤10 页（遵守既有 pages[] 上限）。更大的 feature 驱动产品走
  atlas 交付路径——planner 明说，绝不静默截断。

## 模板治理（template governance）

- `descriptor_version`（缺省=1）+ `upgradeTemplateDescriptor`：v1→v2 盖章并
  从 slot roles 派生 `required_furniture`；幂等；未知未来版本原样保留并记
  problem。catalog 载入时自动迁移（`loadProblems()` 可见）。
- 生命周期：`deprecated: true` + `replaced_by: "<template id>"`——实例化时
  盖章 `template_lifecycle` 进草稿（仅告警，不拒绝）。
- 契约执行：`required_furniture`（title/legend/scale_bar/north_arrow/
  data_source/map，≤16 条）随实例化盖章 `template_required_furniture`；
  preflight 新规则 `MAP_REQUIRED_FURNITURE_MISSING` 用 semantic_role 前缀核
  对，repair 路由到既有 add_* 家具修复。

## 新 preflight 规则（append-only）

| 码 | 语义 | repair |
|---|---|---|
| `MAP_TINY_FONT_PRINT` | 声明 print 交付（output.dpi≥300）时注记类文字 <7pt | 提到 7pt |
| `MAP_ALIGNMENT_DEVIATION` | 同集合同页同宽条目 x 漂移 0.5–3mm | 吸附到 peer |
| `MAP_WHITESPACE_IMBALANCE` | 左右留白比 >2.5× 且大侧 >12mm | 平移整块内容 |
| `MAP_REQUIRED_FURNITURE_MISSING` | 模板契约缺家具 | add_* 家具修复 |

（原计划 `MAP_LEGEND_TRUNCATION` 与既有 `MAP_LEGEND_DENSITY` 语义重复，未
新增。）

## GUI 异步化

制图工作台不再在 GUI 线程执行任何算子：`submitOperatorJob` 经
TaskCenter 提交（JobEngine 按 algorithmId 走 RSOperatorRegistry——与
workflow 节点同路径），带进度镜像、busy 门控与「停止」按钮（协作取消）。
新增「生产导出」按钮驱动完整 produce 链。

## 复核一交付

```text
1. 读 <base>.<format>.manifest.json（校验 + digest 复算）
2. 对每个 pages[i]：磁盘文件重算 sha256/bytes 比对
3. structural_digest 与 compose 侧报告比对
4. environment 段如实展示但不参与 digest
```
