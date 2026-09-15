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
