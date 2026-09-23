# 现状矩阵 — Workbench/Project Lifecycle Shell（本 track 后）

每项：权威数据源 → 修复 → 测试（fixture 形态）→ sabotage oracle。

## 1. 项目打开事务（openProject 核心）

| 项 | 内容 |
|---|---|
| 权威 | `workbench/project_session_boundary.{h,cpp}`（`sicnu_qgis_display` 静态库成员）——probe→clear→story-boundary hook→store bind→read 单序列；窗口只渲染 typed outcome |
| 修复 | ReadFailed 回滚：`project.setFileName(QString())` + `ProjectContext::closeWorkspaceStore()`（新增：closeStore+clearCachedDocument）；窗口失败分支补空会话渲染（setLayers({})/refresh/empty states/title/browser/status “session reset”） |
| 行为保持 | probe 先行（#1083）、hook 恰一次（#1269 B1 语义）、store-open 失败=警告不阻断（Governance 3.0）逐字保持 |
| 测试 | `test_project_session_boundary`（headless `ProjectContext::createHeadless` + 真实 QgsProject 文件 + 真实 SQLite store；readFn 注入只用于真实 parser 无法触达的 mid-transaction 失败分支——先例 `createForTesting` probe seam） |
| Sabotage | 去掉回滚两行 → `read failure rolls back` 红（fileName==target ∧ store open）；hook 计数断言杀“失败重入 hook” |

## 2. Secondary view 会话（B2）

| 项 | 内容 |
|---|---|
| 权威 | `shell/secondary_map_view_session.{h,cpp}`——widget+engineViewId+syncController 三态唯一定义点；窗口成员 `m_secondaryMapView`/`m_secondaryViewId`/`m_dualViewportSync` 删除，槽变薄壳 |
| 修复 | sync 控制器在**每次 open()** 重建（close 删之，widget 存活复用）；open 失败回滚到一致 closed（tearDownSync+unchecked）；widget 连接只在 ensureWidget 建一次 |
| 测试 | `test_secondary_map_view_session`（真实 canvas 对 + 真实 ProjectContext + 真实 controller：extent 跟随、stats==1/1 防重复连接、close→reopen kill、project clear 存活、dtor 释放 view） |
| Sabotage | sync 创建塞回首建分支 → `reopen re-creates sync` 红（syncController null）；连接移入 open() → stats==1 断言红 |

## 3. Layout designer 生命周期（B4）

| 项 | 内容 |
|---|---|
| 权威 | `layout/qgslayoutdesignerdialog.cpp` 构造器内 layout QObject `destroyed` 连接——layout 死即 mMasterLayout/mLayout 置空 + close()（唯一创建点 newLayout 无需跟踪成员；所有 layout 销毁路径统一走 QObject::destroyed） |
| 修复面 | designer 自防；窗口侧零新增状态（无重复 truth） |
| 测试 | `test_layout_designer_lifecycle`（真实 manager-owned layout + 真实 designer offscreen：removeLayout / project.clear() / unmanaged delete 三路退休 + 事后 no-op 安全 + WA_DeleteOnClose 全链退役（QPointer isNull）） |
| Sabotage | 还原 designer cpp（master 码）→ 三 CASE 红（窗口仍开、layout() 非 null）——即 **current-master RED** |

## 4. SpatialTool 注册收敛（B6/B7 修正版）

| 项 | 内容 |
|---|---|
| 权威 | `SpatialToolRegistry`（进程级）+ `RegistrationToken`（arm/release/move；析构即注销）；窗口两处注册持 token 成员（析构自动释放） |
| 语义保持 | first-wins 不变（重复 arm 被拒、原注册完好）；in-flight executor 持 shared_ptr 完成 execute；guard（QPointer payload）不变 |
| 测试 | `test_spatial_tool_registration`（fake 工具语义组 + `WorkbenchContextTool` 真形接线：注册→execute→退役→find null） |
| Sabotage | token.release() 空转（还原无注销）→ `unregisterTool`/`retires` 红 |

## 5. 明确未做（记录）

- B9（LayerTreeMenuProvider 泄漏）、B11（auto-load timer 模态）、B12（restoreState 返回值）、B13（layerWasAdded 重复连接）、B14（saved toast 矛盾）——#1269 记录，不属本 track 深度目标。
- `saveProjectAs` 失败路径已事务化（#1097），未改。
- 失败 open 可能已在目标旁创建空 `.governance.db` 文件（openWorkspaceStore 先于 read 是 v3 恢复契约要求的顺序）；本 track 只解绑不删文件（删除用户目录文件超边界）——已记录。
- 全 shell（完整 QgisDesktopWindow）offscreen fixture：构造器单体（plugins/ribbon/theme），本 track 以四个行为级最小 fixture 交付；全窗口 fixture 仍为后续方向。
