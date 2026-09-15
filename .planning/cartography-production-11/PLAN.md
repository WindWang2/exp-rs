# PLAN — cartography-production-11

基线 `origin/master@a5b11b7f`。审计结论驱动的 8 个工作包（WP）映射与执行顺序。
原则：不重复造轮子（引擎唯一、四 surface 已同引擎）；每个 WP = 现状证据 → 设计选择 → 最小 vertical slice → known-answer/negative test → failure/cancel/resource → docs 同步 → commit。

## WP → 缺口映射（见 BASELINE.md Top10 缺口）

| WP | 交付 | 依据缺口 |
|---|---|---|
| A 异步生产 job | `cartography:produce` 单算子编排 compose→preflight→repair(有界)→export（同一引擎自由函数，零第二实现）；TaskCenter 后台执行；进度/取消/原子发布（产物+manifest 同步落盘）；CartographyDock 改走 GuiJobHandle（既有 seam）异步提交 + 进度 + Stop | 缺口 1/3/9 |
| B Atlas/series | export 内实现 atlas 逐要素迭代（单一导出权威）；`cartography:produce` 支持 atlas 导出 + AtlasManifest（页名/要素/extent/sha256）；series 定义（vector/region/time 表）→ 多页 MapSpec 生成器（页变量/extent/标题/页码/索引页）；known-answer fixture（内存 scratch layer） | 缺口 2/6/10 |
| C 模板治理 | 模板/组件 descriptor 版本迁移机制（对照 upgradeMapSpec 模式，additive `descriptor_version` + 迁移步骤注册）；生命周期（deprecated/replaced_by）+ 校验；组件"必需家具"契约（title/legend/scalebar/north arrow/data source）进 preflight | 缺口 5/10 |
| D 布局质量深化 | additive 规则：对齐容差、留白均衡、dpi 感知最小字号、图例截断；每条规则有界 repair + 台账 | 缺口 9（扩展） |
| E 图表/注记 provenance | chart/annotation 的 binding 来源（layer/field/expression）记录进 compose 结果与导出 manifest；annotation 锚定地图坐标 callout 组件 | 缺口 4（关联） |
| F 确定性导出 | 导出 manifest sidecar（atomic tmp+rename；不含 wall-clock 的 digest）；PDF/SVG 设置硬化（forceVector、text format、metadata title/author）；字体策略（structured 替换诊断 + `require_no_font_substitution`）；环境差异诚实报告（Qt/QGIS/OS/字体）写入 manifest 非 digest 段 | 缺口 4/7 |
| G headless preset E2E | 同一 MapSpec fixture 走三 surface：agent tool 调用、RSOperatorRegistry 派发、TaskCenter pipeline（CLI 同路径）→ 产物逐字节一致 + manifest 一致 | 缺口 8（验证而非新面） |
| H 模板 corpus QA | 全部 shipped templates（56）instantiate→upgrade→validate→preflight→repair(有界)→compose→export(png 低 dpi) smoke；atlas 模板加 atlas 导出断言；诚实 degraded 清单 | 缺口 2/5 验证 |

## Phase 计划

| Phase | 内容 | WP |
|---|---|---|
| 0 | 审计/worktree/artifacts/首次构建 | — |
| 1 | 契约层：export manifest 数据模型 + produce 结果契约 + template descriptor version/migration 契约（Qt-free 纯数据+JSON） | A/B/C 契约 |
| 2 | 执行第一大块：atlas 逐要素导出 + produce 编排 + 取消/进度/原子发布 | A/B |
| 3 | 执行第二大块：series→多页 MapSpec 生成器 + 模板迁移 + 新 preflight 规则 + annotation/chart provenance | B/C/D/E |
| 4 | surface：produce 接入 agent tools + operators + dock 异步化（GuiJobHandle）+ docs/help 同步 | A/G |
| 5 | 硬化：失败矩阵、取消中途一致性、只读目录、Unicode 路径、重复 produce 幂等、有界规模 | A/B/F |
| 6 | E2E + corpus QA（WP G/H）+ drift gate | G/H |
| 7 | 独立 adversarial review（subagent #2）+ P0/P1 修复 | — |
| 8 | 双验证、rebase、PR | — |

## 测试基座

- 目标：`test_mapspec`（聚合既有 cartography 套件，Catch2）。新增套件进同一目标（或独立 `test_cartography_production_11` 目标，倾向后者以隔离回归）。
- Oracle 独立性：页数/文件名/sha256 由测试自行重算（不读 produce 返回值断言自身）；manifest 与磁盘文件交叉验证；至少一个 negative（取消后无产物、非法 series 拒绝）。

## 风险与对策

- **MSVC 构建断点**（data_platform_tools.cpp）：1 行 build-unblock（D-007）。
- **QgsLayoutExporter atlas API**：QGIS 原生 `QgsLayoutAtlas::beginRender/next`；以 LayoutService 既有模式包裹；如 API 不可用降级为自管 feature 迭代（reset composer + filter expression），在 DECISIONS 记录。
- **GUI 异步化回归**：dock 保持零引擎逻辑；异步仅换 dispatch 层；进度经 taskUpdated 信号。
- **manifest 与导出非原子窗口**：产物 rename 成功后写 manifest；manifest 失败→产物标记 `manifest_missing` 并回滚产物（删除刚 rename 的文件），保证"取消/失败无半成品"。
