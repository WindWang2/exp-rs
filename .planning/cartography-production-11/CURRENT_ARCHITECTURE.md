# CURRENT_ARCHITECTURE — cartography-production-11（基线 a5b11b7f 现状 + 本 track 变更）

## 基线 authority/seam 图

```
                    ┌── agent tools (22× cartography:*)  cartography_tools.cpp
   MapSpec v5 ──────┼── RSOperator (5× cartography:*)    cartography_operators.cpp ── RSOperatorRegistry
   (mapspec/)       │         │                                        │
   validate/upgrade │         └──── 同一组自由函数 ◄───────────────────┤
   conditions       │                                                  TaskCenter（唯一执行器；
                    │                                                  CLI --pipeline / GUI dialog /
                    ├── composition.cpp   bounded solver (hard+soft)   workflow node 都经此）
                    ├── quality.cpp       preflight(~40 规则)/repair 台账
                    ├── registry.cpp      components/templates catalog（extends/facet/diff）
                    ├── style_spec/style_compiler/design_tokens/typography
                    ├── chart_registry.cpp  inline QPainter charts + vector_expression
                    └── export.cpp        png|pdf|svg；tmp→sha256→rename 原子；无 atlas/无进度/无manifest

   GUI: src/app/cartography/cartography_dock.cpp ──同步── op->run()（缺口：不经 TaskCenter）
```

## authority 判定（本 track 遵守）

- MapSpec 文档/版本权威：`src/agent/mapspec/mapspec.{h,cpp}`（kMapSpecCurrentVersion=5，upgradeMapSpec）
- preflight 规则权威：`quality.cpp`（rule catalog + preflightRuleCatalog）
- 导出权威：`export.cpp`（唯一写产物处）
- 目录/模板权威：`data/cartography/**` + `registry.cpp`（index.json drift gate）
- 执行权威：`src/processing/framework/task_center.{h,cpp}`（不建第二执行器）
- 图表权威：`chart_registry.cpp`（kind/上限）

## 本 track 新增（交付后回填，格式：能力 → 文件 → seam）

- export manifest 数据模型/写入 → `src/agent/cartography/export_manifest.{h,cpp}` → 被 export.cpp/produce 消费
- atlas 导出 → `export.cpp: exportMapAtlas` → produce/算子/tool 同一入口
- produce 编排 → `src/agent/cartography/produce.{h,cpp}` → `cartography:produce` 算子 + agent tool + dock
- series 生成器 → `src/agent/mapspec/series_planner.{h,cpp}`（纯函数）+ agent tool
- 模板迁移 → `registry.cpp: upgradeTemplateDescriptor` + `descriptor_version`
- dock 异步 → `cartography_dock.cpp`（GuiJobHandle 提交，零引擎逻辑）

## 交付后回填（2026-09-16，实际落地）

- **export manifest** → `src/agent/cartography/export_manifest.{h,cpp}`：payload/`manifest_digest`（环境段豁免）/`validateExportManifest`/`writeExportManifest`(原子)/`readExportManifest`(digest 校验)。
- **atlas 导出** → `export.cpp: exportMapAtlas`（+`validateMapAtlasExportRequest`、`sanitizePageStem`、`featureExtentJson`）：QgsLayoutAtlas beginRender/next 逐页 temp→sha256→rename；页边界取消探测；512 页守卫；失败/取消整单回滚。png-only；pdf/svg atlas 版式走原 exportMapLayout（mode "single"）。
- **produce 编排** → `src/agent/cartography/produce.{h,cpp}`：upgrade→validate→resolveCompositionPass→compile→preflight→有界 repair（重编译）→export(single|atlas)→manifest；类型化 error_code；取消/失败目录零残留；`produceResultToJson`（output=manifest 或 artifact）。
- **共享 pass 提升** → `composition.{h,cpp}: resolveCompositionPass / composeProvenance`：tools/operators 中的两份内联实现合并为引擎级唯一实现（produce 也复用）；composeProvenance 增 bindings 溯源（≤64）。
- **series planner** → `src/agent/mapspec/series_planner.{h,cpp}`：table/vector → 多页 MapSpec（v6）；{{token}} 替换、-p<k> 克隆+引用重映射、index 页；10 页上限导向 atlas。
- **MapSpec v6** → `mapspec.h` (kMapSpecCurrentVersion=6) + `mapspec.cpp`（pages[].variables/series_row/crs 校验 + role "index"）。
- **模板治理** → `registry.{h,cpp}`：`kTemplateDescriptorVersion=2`、`validateTemplateGovernance`、`upgradeTemplateDescriptor`（载入时自动迁移）、`templateLifecycle`、`requiredFurnitureRolePrefix`；instantiateTemplate 盖章 template_lifecycle/template_required_furniture。
- **新 preflight 规则** → `quality.cpp`：MAP_REQUIRED_FURNITURE_MISSING（repair 复用 addFurniture lambda）、MAP_TINY_FONT_PRINT、MAP_ALIGNMENT_DEVIATION、MAP_WHITESPACE_IMBALANCE（均含 repair 分支 + catalog 条目）。
- **annotation 锚定** → `mapspec_compiler.cpp` annotations 循环：map_ref+anchor(+leader 样式) → `<id>-anchor-line` polyline（locator-connector 同一几何契约）。
- **dock 异步** → `src/app/cartography/cartography_dock.{h,cpp}`：`submitOperatorJob`（TaskCenter submitJob，JobEngine 按 algorithmId 走 RSOperatorRegistry——与 workflow 节点同路径）、进度镜像、busy 门控、停止按钮、`runProduce`。
- **surfaces** → `cartography_operators.cpp: ProduceOperator`（cartography:produce 第 6 算子）、`cartography_tools.cpp: ProduceTool`（第 23 个工具）。
