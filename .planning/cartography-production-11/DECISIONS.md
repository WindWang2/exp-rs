# DECISIONS — cartography-production-11

 autonomy=full 下全部就地裁决；每条含备选与理由。

## D-001 基线刷新：#991/#992 已合并，基线取 a5b11b7f
Prompt 快照（#991/#992 open）已失效。按 GOAL 规则 3，以启动时 `origin/master@a5b11b7f` 为唯一事实源；D18/D19 能力已在基线内，不视为并发。备选：等待旧 PR —— 违背"不等待"，否决。

## D-002 async 只换 dispatch 层，不建第二执行器
GUI 异步化通过既有 `GuiJobHandle`/`RsJobRunner`（master 稳定 seam，注释明确"must not bypass Task Center"）提交 cartography:* 算子；不新建线程池/scheduler。备选：dock 内 QThread 直跑 —— 造成第二执行路径 + 取消/进度不一致，违反 Oracle 1，否决。

## D-003 `cartography:produce` 是编排不是引擎
produce = 按序调用既有自由函数（validateMapSpec/resolveComposition/preflightMapSpec/repairMapSpecWithLedger/MapSpecCompiler::compile/exportMapLayout）+ 进度/取消编织 + 原子发布。与 5 个既有算子共享全部逻辑；新增行为仅"序列编排+发布契约"。备选：GUI/agent 各自编排 —— 第二套流程逻辑，否决。

## D-004 atlas 导出实现于 export.cpp（单一导出权威）
逐要素迭代（QgsLayoutAtlas::beginRender/next 或等价显式迭代）加进既有 export.cpp 的新入口 `exportMapAtlas`；单页 `exportMapLayout` 保持原语义。manifest（每页 path/sha256/feature id/filename）由同一处产出。备选：独立 atlas 导出文件/工具 —— 分裂导出权威，否决。

## D-005 发布契约 = 产物原子 + manifest 原子 + 一致性
产物沿用既有 tmp→校验→rename；manifest sidecar 同模式。顺序：所有产物 tmp 写完→校验→逐个 rename→最后 rename manifest。失败/取消：删除全部 tmp 与已 rename 产物（回滚），不留半成品（Oracle 3）。digest 段排除 wall-clock；环境段（Qt/QGIS/OS/字体）如实记录但不入 digest。

## D-006 模板版本迁移采用 upgradeMapSpec 同构模式（additive）
descriptor 增加 `descriptor_version`（缺省=1），`upgradeTemplateDescriptor` 注册表 v1→v2… 逐级迁移，幂等；未知版本=校验错误。生命周期字段 `deprecated`/`replaced_by` 仅告警+catalog 标注，不改实例化行为（向后兼容）。备选：重写 registry 载入 —— 破坏面大，否决。

## D-007 跨域 build-unblock：data_platform_tools.cpp 1 行
master a5b11b7f `BenchmarkService` 无限定使用（sicnu::experiment）无 using-directive → sicnu_agent 编译失败（#1009 已证并同款修复）。本 track 需构建 sicnu_agent → 添加 `using namespace sicnu::experiment;`（含注释），PR_BODY 顶部声明 OUT_OF_SCOPE build-unblock + dedupe #1009。备选：等 #1009 merge —— 违背"不等待"，否决。

## D-008 新 preflight 规则码 append-only
新增：`MAP_ALIGNMENT_DEVIATION`（同集合同页同宽条目 x 漂移 0.5–3mm；repair 吸附到 peer）、`MAP_WHITESPACE_IMBALANCE`（页内容左右留白比 >2.5×；repair 平移整块内容）、`MAP_TINY_FONT_PRINT`（声明 output.dpi≥300 时注记类文字 <7pt；repair 提到 7pt）、`MAP_REQUIRED_FURNITURE_MISSING`（模板治理契约，见 D-006/D-014）。**原计划的 `MAP_LEGEND_TRUNCATION` 取消**：与既有 `MAP_LEGEND_DENSITY`（图例矩形容量检查）语义重复，避免双码同义。全部规则有界（对齐检查 100 条上限）、repair 走既有台账机制。备选：复用旧码加参数 —— 破坏既有 report 消费者，否决。

## D-014（追加）模板治理契约进 preflight
`instantiateTemplate` 把模板的 `required_furniture` 盖章进草稿（`spec.template_required_furniture`），lifecycle 盖章为 `spec.template_lifecycle`（仅告警不拒绝）；preflight 新规则逐 role 用 semantic_role 前缀核对（title.→title 等，registry.cpp 单一映射表 `requiredFurnitureRolePrefix`）。v1 模板在 catalog 载入时自动迁移 v2（从 slot roles 派生 required_furniture），保证旧语料获得同等执行面。

## D-009 series 生成器为纯函数 + 独立文件
`series_planner`（Qt-free 核心 + 薄 QGIS 读层）：输入 MapSpec 模板 + series 定义（vector layer（feature/region）、显式表（time/region）），输出 N 页多页 MapSpec（页变量/extent/标题/页码/可选索引页）。输出仍是 MapSpec v5（不新造文档格式）；数量上限默认 512 页（可参数化但钳制），防失控。备选：直接驱动 QgsLayoutAtlas 而不物化多页 —— 两者并存：atlas 走 QGIS 原生迭代（运行时），series 走物化（可审计/可 diff），分别服务不同 WP-B 需求。

## D-010 CLI 不加新 verb
headless 可达性已由 `cartography:*` 算子 + `--pipeline` 覆盖；E2E（WP G）以 TaskCenter pipeline 路径验证三 surface 同引擎。备选：新增 `--cartography-produce` —— 新 CLI 面需要 help/catalog/drift 同步，收益低，否决（follow-up 记录）。

## D-011 dock 页面导航最小化
多页/预览增强（page 0 限制）→ 仅加"页码选择 + atlas 导出按钮"最小面；重布局不属本 track。备选：完整 atlas 配置面板 —— UI 范围失控，否决（follow-up）。

## D-012 测试新目标 test_cartography_production_11
独立 qt_add_executable，避免 5000+ 行 test_mapspec 目标增长；复用 sicnu_test_main + Catch2 + SICNU_CARTOGRAPHY_DATA_DIR 模式。既有套件不动（回归锚）。

## D-013 规模上限（防失控硬界）
produce 单作业页数 ≤512；atlas manifest 页数 ≤512；manifest 字段数固定；series 页钳制 512；所有循环有界并可取消（每页边界检查取消）。
