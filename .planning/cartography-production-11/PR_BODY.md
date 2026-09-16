# Cartography Production 11.0 — async production, atlas delivery, series, template governance & deterministic export evidence

> **Local evidence only; no online CI dependency.**
>
> **P0 (out of scope, minimal build-unblock included):** `origin/master@a5b11b7f`
> 的 `src/agent/data_platform_tools.cpp`（PR #992/D19 产物）在 `namespace
> sicnu::agent` 内无限定使用 `BenchmarkService`（`sicnu::experiment`）且无
> using-directive，sicnu_agent 无法编译。本 PR 附带 1 行 build-unblock
> （`using namespace sicnu::experiment;`），与 open PR #1009 的同文件修复
> 同款（dedupe：rebase 时若冲突取其语义即可）。

## Baseline & dedupe

- Baseline：`origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- 启动审计：#991/#992 已在基线内合并；启动时 open PR 为 #1009（execution
  runtime，业务文件与本 track 零交集；共享集成文件按 append-only 处理）与
  #1008（spectral，CONFLICTING；本 track 不触碰其任何文件）。
- Open issues #1001-#1007（io/workflow/dataset/georef 域）逐条 dedupe：均
  不在制图域，登记 OUT_OF_SCOPE（`.planning/.../BASELINE.md`）。
- 本地并行 worktrack `zcode/geoai-promptable-foundation-platform-11` 只写
  `app/lib/modelops/**`（Python）——零交集。

## Why

平台 10.0 已让 compose/preflight/repair/export 四算子在 agent/CLI/GUI 同引
擎可达，但审计确认的生产缺口仍然真实：

1. CartographyDock 在 **GUI 线程同步执行**算子，无进度/取消；
2. **图集逐要素交付零实现**——atlas-guide 宣称的 per-feature 导出没有代码
   支撑（export.cpp 无任何 atlas 迭代）；
3. 导出**证据不持久**：structural digest 只在返回值里，无 manifest，交付
   无法从磁盘独立复核；
4. **模板无版本迁移/生命周期**（对照 upgradeMapSpec），组件契约（必需家
   具）无执行面；
5. 导出内部无进度/取消（dpi1200/A0 不可中断）、无环境诚实记录。

## What lands（WP A-H）

- **A 异步生产链**：`cartography:produce`（第 6 个 cartography 算子 + 第
  23 个 agent tool + GUI「生产导出」按钮）——upgrade → validate → compose
  → bounded repair → export → manifest 的**编排**（引擎自由函数零复制）。
  类型化拒绝码；**原子发布**（失败/取消整单回滚，目录保持调用前内容）；
  CartographyDock 全面异步化（TaskCenter 提交，与 workflow 节点同注册派
  发路径），进度镜像 + busy 门控 + Stop 按钮。
- **B Atlas/series**：`exportMapAtlas`（单一导出权威内）逐要素原子交付，
  每页 sha256/feature_id/label/coverage-CRS extent，页边界取消，512 页失控
  守卫；png-only 能力面诚实（pdf/svg atlas 版式 = 静态全页导出，manifest
  记 mode "single"）；`planSeries` table/vector → 多页 MapSpec v6（页级
  variables/extent/标题/页码/索引页，{{token}} 计划期替换，id 克隆+引用重
  映射，10 页物化上限诚实导向 atlas 路径）。
- **C 模板治理**：`descriptor_version` + 幂等 v1→v2 载入迁移（从 slot
  roles 派生 required_furniture）；`deprecated`/`replaced_by` 生命周期盖
  章；`required_furniture` 契约经 preflight `MAP_REQUIRED_FURNITURE_MISSING`
  执行，repair 路由既有 add_* 家具修复。
- **D 布局质量**：新规则（append-only）`MAP_TINY_FONT_PRINT`（print
  dpi>=300 时 7pt 下限）、`MAP_ALIGNMENT_DEVIATION`（同宽近列漂移）、
  `MAP_WHITESPACE_IMBALANCE`（留白失衡），全部有界 repair + 台账；
  `MAP_LEGEND_TRUNCATION` 因与既有 `MAP_LEGEND_DENSITY` 重复而**未新增**。
- **E 数据驱动溯源**：`composeProvenance.bindings`（binding 条目的
  mode/layer/field/expression，<=64）进 compose/produce 输出与 manifest；
  注记地图锚定（`map_ref`+`anchor`+leader 样式 → 引导线 polyline，locator
  connector 同一几何契约）。
- **F 确定性导出证据**：`export_manifest`（原子 sidecar）：canonical
  payload 的 `manifest_digest`（**环境段豁免**——两台主机的诚实环境描述不
  改变同一交付的身份）、per-page sha256/bytes、structural digest、
  provenance；`readExportManifest` 从磁盘独立复核。
- **G headless E2E**：engine 自由函数 / agent tool / RSOperator+TaskCenter
  三面 byte-equal（同 spec → 同 PNG sha256）；TaskCenter 提交+waitForTask
  证明 pipeline/CLI 同路径。
- **H 模板 corpus QA**：全部 shipped templates instantiate→produce→
  manifest 互证 smoke（failures 断言空；degradations 诚实汇报）。

## 兼容性

- 全部新增为 additive：MapSpec **v6** 是 v5 严格超集（pages[].variables/
  series_row/crs + role "index"），`upgradeMapSpec` 幂等盖章；模板治理面
  全部 optional 成员；新 preflight 规则码 append-only；v1 模板载入自动迁
  移 v2，旧语料获得同等执行面；既有 5 算子/22 工具语义零变化。
- GUI：dock 不再在 GUI 线程执行算子（唯一行为变化；引擎语义不变）。

## Local tests（本机 MSVC/Ninja dev-default，QT_QPA_PLATFORM=offscreen，ctest -j1；ninja -j2 上限）

（Phase 8 填：targeted suites 两轮 exit 与断言数）

## 资源证据

见 `.planning/cartography-production-11/PERFORMANCE.md` 与 `monitor.log`
（60s RSS 采样；Git Bash 无 loadavg，未观测到 >70% RSS 阈值，保持 -j2）。

## Review findings 与处置

（Phase 7 填）

## Known limitations

- atlas 交付 png-only（本 QGIS 构建的 pdf/svg atlas 能力为静态全页文档）。
- 物化 series <=10 页（遵守 pages[] 文档上限）；更大走 atlas。
- atlas 每页 font substitution 诊断未逐页采集（单页路径照旧）。
- dock 预览仍为 page 0（多页预览导航 follow-up）。
- 无新 CLI verb（headless 经 `--pipeline` 的 cartography:produce 步骤已可
  达；CLI 直达 verb 记 follow-up）。

## Follow-ups

- dock 多页/atlas 预览导航。
- `cartography:produce` CLI 直达 verb + help/catalog/drift 同步。
- 逐页字体替换诊断进 atlas manifest。
- 索引页升级为 table chart 组件（当前为有界 label）。
