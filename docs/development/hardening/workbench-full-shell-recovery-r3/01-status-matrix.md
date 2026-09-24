# 现状矩阵 — Workbench / Full-Shell Fixture / Project Lifecycle Recovery R3

实时基线见 [00-recon-baseline.md](00-recon-baseline.md)。每项：权威数据源 → 修复 →
测试（fixture 形态）→ sabotage oracle。

## 0. 中央接线：shell 翻译单元收敛为 `sicnu_geo_rs_shell`

| 项 | 内容 |
|---|---|
| 权威 | `src/app/CMakeLists.txt` 的单一源列表。此前全部 shell TU（main_window*.cpp、
        workbench/、shell/、dialogs/、teaching/ 等）是 `sicnu_geo_rs` 可执行文件的
        私有源——任何测试都无法链接完整 shell，除非复制源列表（#1295–#1309 连环
        direct-TU-embed 漂移的根源）。 |
| 修复 | `add_library(sicnu_geo_rs_shell STATIC EXCLUDE_FROM_ALL)` 承载全部 shell TU
        （含条件块：python embed、classify/obia、teaching）；`sicnu_geo_rs` 只保留
        `main.cpp` + 两个 qrc（RCC 初始化器在静态库里会被链接器丢弃——原注释约束
        保持）。shell 库镜像 exe 的 include/链接面（PRIVATE）；exe 链接行不变。 |
| 测试 | `test_workbench_full_shell_lifecycle` 直接链 `sicnu_geo_rs_shell`——中央文件
        的语义漂移（新增 TU、改构造契约）会让本套件在链接/运行时立刻爆。 |
| 非目标 | 不改产品行为；RCC 约束、python/classify/obia 条件语义逐字保持。 |

## 1. 打开事务（窗口级）— `openProjectFrom`

| 项 | 内容 |
|---|---|
| 权威 | `workbench/project_session_boundary.cpp`（probe→clear→story hook→store→read+
        回滚，#1284）不动；窗口侧 `openProject()` = 确认链+文件对话框+
        `openProjectFrom(path)`（事务+typed outcome 渲染，逐字搬移）。 |
| 修复 | G1：`onSessionEmptied` 钩子内联的三行（stopLabRecording +
        resetMissionSessionState）与 `newProject` 的故事边界合并为
        `resetSessionStoryState()`，并补上 `newProject` 一直有、open 钩子一直缺的
        `setLabRecordingContext(QString(),…)`——ReadFailed 后 cockpit 不再展示
        已死工程的录制上下文。空会话渲染（newProject / ReadFailed 两处重复的
        六连调用）合并为 `renderEmptySessionShell()`，并补 `setMapTool(m_panTool)`
        ——清空后的会话不再保留死工程上的活动编辑工具。 |
| 测试 | `test_workbench_full_shell_lifecycle`（真实 QgisDesktopWindow offscreen）：
        open A→open 损坏 B→标题/fileName/store 仍 A、B 字节不变、saveProject 落 A；
        A→new→B 后 lab 上下文/mission/tool 归零再重绑。 |
| Sabotage | `renderEmptySessionShell` 去掉 `setMapTool(m_panTool)` → story_boundary 用例红
        （tool 仍是 QgsMapToolSelect）；`resetSessionStoryState` 去掉
        `setLabRecordingContext` → story_boundary 用例红（labExperimentDbPath 非空）。 |

## 2. SaveAs / 工程迁移 — `saveProjectAsTo`

| 项 | 内容 |
|---|---|
| 权威 | `QgsProject::write`（temp+rename 原子，qgsproject.cpp:3621+）、#1097 身份
        回滚、`ProjectContext::reopenWorkspaceStore`、
        `armMissionSidecarWatcher`（#1228 item 34）。 |
| 修复 | G3/P1'：mission authority 的 `projectRef` 随 Save As 一起搬家（写前重指、
        失败回滚）。陈旧 ref 不是装饰性问题：`main_window_connections.cpp:327` 在
        `context.projectRef != fileName` 时拒绝 mission reconcile——搬过家的工程
        会静默失去 mission 对账。 |
| 测试 | SaveAs 到 Unicode 兄弟目录：fileName/title/store(B.governance.db)/
        sidecar(B.mission.json)/watcher 绑定/projectRef 全部落 B；SaveAs 到已存在
        **目录**（必然失败）后：身份/store/watcher/projectRef 仍 B、B 字节不变。 |
| Sabotage | 去掉 projectRef 重指 → save_as 用例红（projectRef 仍 A）；去掉失败回滚 →
        失败分支红（projectRef 变成失败目标）。 |

## 3. Run 镜像连接收敛 — `ProjectContext::installRunStateMirror`

| 项 | 内容 |
|---|---|
| 权威 | `WorkflowRunCoordinator::runStateChanged`（进程级单例）→ workspaceService
        镜像（issue #754）。连接原本写在 `openWorkspaceStore()` 里：每次成功打开
        （以及每次 SaveAs→reopen）都追加一条连接，N 次打开后一次状态迁移 =
        N 次 recordRun 写 + N 次 entityChanged 广播（数据因按 id 合并仍正确，
        但写与广播无界增长）。 |
| 修复 | 连接移入两个构造器共用的 `installRunStateMirror()`——恰好一次；store 关闭
        时 recordRun 本就是无害 no-op（`GovernanceStore::runById/upsertRun` 对
        `!m_impl` 直接拒绝）。 |
| 测试 | `test_project_context_run_mirror`（headless）：open 两个 store 后驱动一条
        真实 tracked pipeline，coordinator 发射数 == service "run" 广播数。 |
| Sabotage | 把连接搬回 `openWorkspaceStore` → 用例红（deliveries == 2× emissions）。 |

## 4. B12 — 布局状态恢复契约 — `restorePanelState`

| 项 | 内容 |
|---|---|
| 修复 | `restoreState`/`restoreGeometry` 返回值入契约：失败即丢弃坏 blob（下次
        savePanelState 用真实布局重写），不再每个启动静默重试；版本门 `>=` 收紧为
        `==`（更新 shell 保存的状态不应被旧二进制解释）；`kShellLayoutVersion`
        从三处字面量收敛为单一常量（save/restore/reset 共用）。 |
| 测试 | full_shell 的 restore_state 用例：毒化 version-11+坏 state/geometry →
        构造窗口后 blob 被丢弃、版本仍在；version-99+state → 状态被丢弃、版本
        重写为 11。 |
| Sabotage | 忽略 restoreState 返回值（还原 master 写法）→ 用例红（坏 blob 仍在）。 |

## 5. 明确未做（记录）

- **ExperimentStudioDock / LabCockpitDock.m_session 的跨工程状态**（recon #2）：
  两者刻意不随工程走（用户自选外部 store / 课程级 session），所有使用点均已
  fail-closed 重校验；加工程边界钩子属于 UI 行为变更，超出本 track（"不进行
  UI 大改"）。已记录为后续讨论项（project-id 戳记）。
- **失败 open 可能已在目标旁创建空 `.governance.db`**（#1284 记录）：v3 恢复契约
  要求 store-open 先于 read 的顺序保持不变；删除用户目录文件超边界。
- **TaskCenter in-flight 任务的窗口级 shutdown 对话框驱动**：`shutdown_policy`
  纯函数层已有单元测试；本 track 通过真实 `close()` + 拒绝关闭的 dirty bench
  覆盖了 Stage-1 拒绝路径，Stage-2 对话框交互留给人机验证。
- 全窗口构造在无 `initQgis()` 环境下的插件/浏览器 dock 行为：空 provider 列表
  只是空面板，不崩溃（offscreen 实测通过后记录于 02-test-ledger.md）。
