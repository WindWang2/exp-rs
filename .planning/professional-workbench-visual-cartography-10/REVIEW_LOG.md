# REVIEW_LOG — 对抗审查记录

审查设置：主 agent 自审 + 2 个只读 subagent（A：架构/语义/历史去重/所有权；
B：测试可信度/性能/并发/移植/文档漂移）。范围 `7d78059d1a..HEAD`（69 files, +6312/−89）。
两位 reviewer 合计原始结论：A：0 P0 / 4 P1 / 4 P2 / 6 P3；B：0 P0 / 1 P1 / 3 P2 / 10 P3。
去重后逐条处置如下（全部经主 agent 在代码中复核）。

## P0

无。

## P1（全部修复）

| # | Finding（来源） | 处置 | 证据 |
| --- | --- | --- | --- |
| P1-1 | 算子目录 open 路径死连接：connect 被 `if(m_sessionController)` 包裹，而控制器在其后才创建（A1） | **已修**：移除外层守卫，判空移入 handler 并注明顺序原因 | `main_window_workbench.cpp` catalog wiring 块注释；`test_processing_catalog_ux` 绿 |
| P1-2 | cartography preset 占位符 `${carto_raster}`/`${output}` 用 artifact 名，TaskCenter/coordinator 路径不解析（A2） | **已修**：改 step-qualified `${carto_import.output}` / `${carto_compose.output}` | `preset_catalog_widget.cpp`；与 `placeholder_grammar` stepId.portName 形态一致 |
| P1-3 | cartography 算子与 agent 工具语义漂移：resolve 作用在副本上、preflight 未先 resolve、repair 单趟且键名不同（A3） | **已修**：`resolveCompositionPass` 改为 in-place 并返回 composition JSON；Preflight/Compose/Repair 逐一对齐工具契约（repair 为有界循环 + repairs_applied/iterations/repair_ledger/quality） | `cartography_operators.cpp`；`test_cartography_operators_10` 5/5 绿（断言已更新为工具同形） |
| P1-4 | VaDataSource 池线程 raw `this` marshal → 析构后 use-after-free（A4/B1） | **已修**：池 lambda 捕获 `QPointer<VaDataSource>`，marshal 目标经 `self.data()` 重查（histogram_widget 既有模式）；stale 中的 this 仅作 owner key（不接触）保持不变 | `va_source.cpp`；`test_visual_analytics` 5/5 绿 |

## P2（全部修复）

| # | Finding（来源） | 处置 | 证据 |
| --- | --- | --- | --- |
| P2-1 | WorkbenchContextTool 首注册保留语义 + raw this 捕获 = 潜在 UAF 陷阱；注释断言与语义相反（A5/B3） | **已修**：provider 改 QPointer 重查目标；注释改写为如实描述；测试收紧为 first→!second | `main_window_workbench.cpp`；`test_agent_workbench_context` 17 断言绿 |
| P2-2 | availableCommands 契约措辞夸大（未按可用性过滤）（A6） | **已修（措辞如实化）**：header/工具描述改为"已注册命令词汇表，非可用性保证"；按事实逐命令过滤需 registry 新 API，记 follow-up | `object_identity.h`、`agent_context_tool.cpp` |
| P2-3 | ARCHITECTURE.md 多处与代码不符（cursor/visibility 联动、MapLayout/Experiment kind、selection hub、上限数字、错误码名、放置位置、注册方式）（A7/B4） | **已修（文档改真）**：ARCHITECTURE.md 重写为 As-Built；未交付能力如实列为 follow-up | 见 ARCHITECTURE.md 修订 |
| P2-4 | setLinked 对新联动视图 raw 拷贝 extent（跨 CRS 为垃圾坐标）（A8） | **已修**：改用 transform-aware 的 propagateFrom(peer) | `view_link_controller.cpp` |
| P2-5 | painter 容量豁免：outliers 无上限、matrix cells 形状不校验（B5） | **已修**：outliers 以 kMaxDrawnPoints 封顶；matrix cells 尺寸守卫（不匹配渲染空而非 assert） | `va_chart_widget.cpp` |
| P2-6 | test_view_link 目标重复编译 qgis_display_manager/auth_resolver TU（B12） | **已修**：从目标源清单移除（sicnu_qgis_display 已含） | `tests/CMakeLists.txt` |
| P2-7 | test_object_identity 分层解析节是死断言（B2） | **已修**：节改为真实覆盖 branch 3（注册图层 → findByPath 命中 + 未注册路径诚实为空） | `test_object_identity.cpp` 66 断言绿 |

## P3（修复 6 项，接受 10 项并说明理由）

已修：
- P3-1 目录面板最近项与全量列表重复显示（A10/B6）→ pinned 去重。
- P3-2 Dock `catch(...)` 缺失 + export 返回无 path 时的空路径播报（B10）→ 兜底 + 守卫。
- P3-3 ExportOperator 缺 pages 回显（A12 部分）→ 与工具一致补齐。
- P3-4 view_link_controller.h 头注释声称 rotation（B13）→ 改为 scale。
- P3-5 test_object_identity Fixture 泄漏 /tmp store 目录（B9）→ 析构 removeRecursively。
- P3-6（随 P2-3）ARCHITECTURE/docs 中的 overstatement 全部改为 As-Built。

接受（附理由）：
- A9/B8 `experiment./dataset./model./workflowrun.` 前缀暂无注册命令：规则层是 register-and-reason 契约的一部分（新命令族注册即得 reason），保留并注明 reserved。
- A11 removeView 后 extentsChanged 连接残留：仅使 stats 计数增长（无行为影响）；viewAboutToBeRemoved 已先Detach；清理需保存 QMetaObject::Connection 表，收益低。记 follow-up。
- A12 部分 Dock 同步运行可能长阻塞 UI：v1 接受（compose 在有界文档上运行；工作流路径本就异步经 TaskCenter）；Dock 后台化记 follow-up。
- A13/B5 部分 painter 信任 bins+1 不变量：生产者契约已在测试固定；防御性分支只加在高危处（matrix）。
- A14 全 nodata 波段以 0 绘制、profile 失败时波段下拉退化：诚实标注为空态改进，记 follow-up（空态已可见为文本）。
- A14 部分 outputSchema 未列 selected* 数组：schema 为宽松形状描述，补列收益低。记 follow-up。
- B11 selectStep 移动 session 游标：产品语义「选中节点 = 设为当前步」与 Run 按钮行为一致（wizard 流），保留；已在代码注释注明。
- B7 目录面板含 cartography:*（registry 即目录，标题"遥感算子目录"）：接受——cartography 步进本就是工作流一等节点（C-1），过滤反而制造第二词汇；docs §34 措辞已改为"registry 全集"。
- 渲染确定性用例本机失败（master 同）：环境字体差异，OUT_OF_SCOPE（见 EVIDENCE）。
- test_processing_catalog_ux "zonal" 断言对将来算子描述的脆弱性：断言限定 id 包含，当前安全；如需再收紧记 follow-up。

## Clean areas（两 reviewer 共同确认）

resolveSelectionAssetTargets 重构零行为漂移（逐行对照原级联）；SelectionContext 扩展沿用
push-notify 既有模式；MCP 请求主线程模型成立、`workbench:` 前缀路由一致、工具只读；
primary-object 规则确定性并被测试固定；无第二权威（SelectionContext/CommandRegistry/
SchemaForm/MapSpecCompiler/RsScanPool/preflight 均单一）；VA 采样走 RasterReader
band-major 布局正确、单 reader 复用、内存有界、token 配色零 QColor 字面量；m_busy 状态机
无 stale-flip 竞态；QSettings 测试隔离正确；openBareOperator 注册有界（insert_or_assign）；
i18n 期望修复逐串核对全部命中；CMake 全部目标 fresh configure 可链接。
