# R18-D TASKS（工作包）

来源：两份只读审计（2026-10-19，基线 a60c8831c）。每包 = RED → 极小修复 → GREEN×2 → 原子 commit。

## Workbench / 交互状态（审计 W）

- [ ] **WP-W4**（P2, 高可测）`toggleEditing` 忽略 `startEditing()` 返回值：失败后 lease 泄漏 + 状态栏谎报 "Stopped editing"。src/app/main_window_vector.cpp:527-539。RED: test_r14_D_vector_edit_honesty.cpp（readOnly 层 start 失败路径）。
- [ ] **WP-W2**（P1）Digitizing 菜单 7 个编辑动作不受可用性 gating（Scale/SplitPart/AddPart/DeleteRing/TrimExtend/ChamferFillet/FeatureArray）。main_window_menus.cpp:230-277 vs 797-824；契约注释 main_window_vector.cpp:279-281。RED: test_r14_D gating 节。
- [ ] **WP-W3**（P1）`editingActive` 派生要求"树选中∩画布当前层"，脱节时 saveEdits 被禁、toggle 显未勾选 → 对活跃编辑会说谎（再点会误停会话）。selection_context.cpp:154-158,683-729。RED: test_selection_context / test_r14_D。
- [ ] **WP-W1**（P0, 拆分执行）属性表/QgisApp 门面编辑绕过 Edit Lease + shell 零监听 editingStarted/editingStopped。qgis_app_facade.cpp:181-278。本轮先做 shell 侧监听同步（状态诚实），完整 lease 路由视风险评估。
- [ ] **WP-W6**（P2）`hasInFlightTask`/`taskInFlight` 不随 TaskCenter 事件刷新（装配时注入一次）。main_window_workbench.cpp:188-213、main_window_connections.cpp:107-111。RED: APP-9B 夹具。
- [ ] **WP-W5**（P2）`editing:state` agent 工具生产恒空（RsEditSession 无生产 attachLayer）。视 W1 进度决定。
- [ ] **WP-W7**（P3）ClassifySessionAdapter::requestCancel 恒 true，无法上报 backend 拒绝。
- [ ] **WP-W8**（P3）工具栏 Select 非编辑态被禁（菜单孪生却 enabled）——能力谎报为不可用。

## MapSpec / Cartography / 出图（审计 C）

- [ ] **WP-C2**（P1, 有契约先例）titles/labels/source_notes/annotations 的 `text` 为数字时静默渲染成占位 "Map title"。mapspec.cpp:879-882 → mapspec_compiler.cpp:891-915 → layout_service.cpp:850。同类先例：chart_registry.cpp:200-204、mapspec_compiler.cpp:865-887。RED: test_mapspec.cpp:846 旁。
- [ ] **WP-C5**（P2）legend `title` 数字 → 属性序列中断 → `linked_map` 被跳过，legend 镜像错误 map。mapspec_compiler.cpp:1051-1054。RED: test_mapspec.cpp:311 旁。
- [ ] **WP-C3**（P1）异构多页修复用 page-0 几何 → 修复不收敛/错误钳位。quality.cpp:1479-1480 vs 274-291。RED: test_cartography_quality.cpp:193 旁。
- [ ] **WP-C1**（P1）单选非首页 PNG 导出必然失败 + 孤儿文件 `<tempbase>_2.png`。export.cpp:227-236,363-415。RED: test_platform9.cpp:1401 旁。
- [ ] **WP-C4**（P1, 谨慎）legend 危害守卫信任声明 layers，声明无实体层时可直达已知 paint 死循环（避免真挂的断言式 RED）。produce.cpp:102-116。
- [ ] **WP-C6**（P2）atlas `filename_expression` 编译期吞错 → 静默 p1/p2 文件名 + 空 manifest label。mapspec_compiler.cpp:375-380。RED: test_cartography_production_11.cpp:786 旁。
- [ ] **WP-C7**（P3）layout:export DPI 守卫非 NaN 安全 + (0,72) 与治理链不一致。layout_service.cpp:1357。RED: test_layout_tools.cpp:667 旁。
- [ ] **WP-C8**（P3）toMm 负坐标钳 fallback → connector/leader 几何静默改 0。layout_service.cpp:112-115。

## 顺序

W4 → C2 → C5 → W2 → W3 → C3 → C1 → W6 → C6 → C7 →（W1 子集 / C4 / W5 / W7 / W8 视余量）。
