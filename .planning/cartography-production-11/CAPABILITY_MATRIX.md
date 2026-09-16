# CAPABILITY_MATRIX — cartography-production-11

状态词汇：implemented / not-supported / degraded / planned(本 track) 。before = master@a5b11b7f。

| 能力 | before | after(目标) | 证据入口 |
|---|---|---|---|
| compose/preflight/validate/repair/export 算子（5） | implemented | 不变（回归锚） | test_cartography_operators_10 |
| GUI 同步执行 | implemented（缺口：GUI 线程阻塞、无取消/进度） | **异步经 TaskCenter + 进度 + Stop** | test_mapspec/dock 逻辑审查 + 新 UI-无关 seam 测试 |
| 单页导出原子性 | implemented | 不变 | 既有 |
| 导出进度/取消（导出内部） | not-supported | **implemented（逐页取消点）** | 新套件 cancel 用例 |
| atlas 配置→QgsLayoutAtlas | implemented | 不变 | atlas-guide |
| atlas 逐要素导出+manifest | **not-supported**（atlas-guide 步骤 5 无代码） | **implemented** | 新 known-answer fixture |
| series(feature/region/time)→多页 MapSpec | not-supported | **implemented（纯函数+钳制）** | 新套件 |
| 导出 manifest sidecar | not-supported | **implemented（原子、digest 无 wall-clock、环境段诚实）** | 新套件 |
| PDF/SVG metadata/矢量/字体策略 | degraded（仅 dpi） | **implemented（forceVector/text format/metadata/require_no_font_substitution）** | 新套件 |
| 模板版本迁移 | not-supported | **implemented（upgradeTemplateDescriptor v1→v2 幂等）** | 新套件 |
| 模板生命周期（deprecated/replaced_by） | not-supported | **implemented（告警+catalog 标注）** | 新套件 |
| 组件必需家具契约（title/legend/scalebar/north arrow/source） | 部分（MAP_MISSING_TITLE 等散点） | **implemented（模板级 required_furniture 契约 + preflight 集成）** | 新套件 |
| 新 preflight 规则（alignment/whitespace/print-font/legend-truncation） | not-supported | **implemented（additive 码+有界 repair）** | 新套件 |
| chart/annotation binding provenance | degraded（结果无来源） | **implemented（进 compose 结果+manifest）** | 新套件 |
| annotation 地图锚定 callout | not-supported | **implemented（锚点+引导线组件）** | 新套件 |
| headless preset E2E（tool/operator/pipeline 三面同引擎） | 部分（operators_10 单面） | **implemented（三面 byte-equal + manifest 一致）** | 新套件 |
| 模板 corpus 全链 smoke | 部分（instantiate→validate→repair） | **implemented（→compose→export→manifest，atlas 模板加 atlas 导出）** | 新套件 |
| produce 失败/取消零半成品 | n/a | **implemented（回滚测试）** | 新套件 negative |
