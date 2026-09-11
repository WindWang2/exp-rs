# REVIEW LOG — professional-workbench-9

## Round 0 — baseline 自审（2026-09-12）

主 Agent 对最新 master 的 UI 安全面做了定点审计，发现（详见 ISSUE_TRIAGE.md）：

| ID | 优先级 | 发现 | 处置 |
|---|---|---|---|
| F1 | P1 | project.new/open/save 菜单裸 action 与 CommandRegistry 双重持有 Ctrl+N/O/S（复查后实际范围更大：ZoomIn/Out/Quit/Ctrl+E/Ctrl+Shift+F/Ctrl+L/H/Ctrl+Shift+I/D/A/C/S/F5 共 19 项） | M2 修复（63d69a60ee） |
| F2 | P1 | pipeline_editor_dock tooltip 声称 Ctrl+N/O/S 但 action 无绑定（#882 残留） | M2 修复 |
| F3 | P2 | histogram_widget GDALOpen 失败静默，无 UI 错误呈现 | M0 修复（c2951a4570） |
| F4 | P1 | roi_statistics_widget::m_requestEpoch 非 atomic，被 pool worker 线程读取（数据竞争 / UB） | M0 修复（std::atomic<uint64_t>） |
| F5 | P1 | test_shortcut_conflicts 只做文件内重复扫描，跨文件（menus vs registry）双权威恰为盲区 | M2 新增 cross-source union gate |
| F6 | P2 | main/build 之前所有 worktree 都假设 shortcut 唯一性由"注册期拒绝"保障，但无人测 installShortcut 的 second-owner 路径 | 由既有 registry 测试 + 新 gate 覆盖（接受） |

## Round 1 — milestone 自审（2026-09-12，实现后逐项复核）

| ID | 优先级 | 发现 | 处置 |
|---|---|---|---|
| R1-1 | P1 | WorkbenchStateModel 首版连接了不存在的 `QgsMapCanvas::mapToolChanged`（fork 用 QGIS 3.x 名 `mapToolSet`） | 编译期暴露，已修 |
| R1-2 | P2 | test_scan_pool 初版 rendezvous 用自旋等待自身完成（死锁缺陷），改为信号量确定性握手 | 已修 |
| R1-3 | P2 | QPointer 在 worker 线程 .data() 与 GUI 线程析构并发，理论上非线程安全（本仓库既有一贯模式） | **有意接受**：对齐指针尺寸的读写在 x86/ARM 上无实际撕裂；彻底方案（weak_ptr 握手）涉及全部 scan 用户，记录为 follow-up，不在本轮大规模翻改 |
| R1-4 | P3 | m_requestEpoch 读比较在 marshaled lambda（GUI 线程）与 worker（原子读）都做，双检查冗余但无害 | 接受（防御性） |
| R1-5 | P2 | M7 分页翻页会 full refresh()（重建整棵树）——200k 索引的 refresh 已被 8.0 的 coalesced timer + light index 保证 <2s 上界；分页未引入新的 O(N²)（切片为 O(window)） | 接受；PERFORMANCE.md 记录上界 |
| R1-6 | P2 | registerPluginCommands 的命令 id 用索引（plugin.<id>.<n>），reload 后 n 可能漂移 | 接受：命令生命周期跟随渲染 action（availability 守卫），palette 展示以 title 为准；稳定 id 需 schema 增加 contribution id 字段，记 follow-up |
| R1-7 | P1 | GDAL 3.13.3 升级使 master 的 canonical_metadata.cpp 无法编译（环境破坏，非本方向引入） | 一行 seam 修复（a6b349d69d），与本文件内既有 size_t 拼写模式一致 |

## Final — adversarial review（2 个只读 subagent，2026-09-12）

### Reviewer A（architecture/correctness/concurrency/security）

| ID | 级别 | 发现 | 处置 |
|---|---|---|---|
| A1 | P2 | invokePluginUi 持 mMutex 阻塞至 15s，可拖住 GUI 线程的下一次 describe/bootstrap | 已修：锁内仅查指针，锁外阻塞调用；UI 投递默认超时降至 2s |
| A2 | P2 | renderer 单例的裸 sink 未在窗口析构清理 | 已修：~QgisDesktopWindow 先行 setShellSink(nullptr) |
| A3 | P2 | plugin.* 命令永不注销；reload 重复注册被拒 → 命令永久失效 | 已修：registry 新增 unregisterCommandsMatching(prefix)；shell release 钩子卸载即清理；重挂载先清后注册 |
| A4 | P2 | gate 盲区：枚举形式绑定不参与冲突扫描；相邻语句兜底不比对令牌 | 已修：collector 增加 enum 分支；窗口兜底需修饰键+键令牌匹配 |
| A5 | P3 | m_lastSelectedAssetId 会"复活"被清空的选择 | 已修：onSelectionChanged 跟踪清空意图 + refresh 期间守卫（含尾部显式调用） |
| A6 | P3 | 空态投影落后画布一个事件循环轮次（收敛正确，可闪一帧） | 接受：收敛语义正确；同步化记 follow-up |

### Reviewer B（tests/performance/portability/docs-vs-code）

| ID | 级别 | 发现 | 处置 |
|---|---|---|---|
| B1 | P1 | TEST_MATRIX 引用不存在的套件；M8 零测试覆盖 | 已修：矩阵重写为与真实 target 一一对应；新增 test_plugin_ui_placement 锁定 M8 命令生命周期 |
| B2 | P2 | M6 注解逻辑无测试；矩阵误标用例 | 已修：applyEnumSourceAnnotations 提升为 provider 公共函数并加注解单测；矩阵行如实改写 |
| B3 | P2 | tool-mode 测试重言式 | 已修：加非 pan 工具转移 + toolModeChanged 计数断言 |
| B4 | P2 | PERFORMANCE 分页行以套件墙钟支撑 O(cap) 声明 | 已修：如实改写为"翻页=完整 coalesced refresh" |
| B5 | P2 | assets 源全量物化再截断，与头文件声明矛盾 | 已修：标签构建到 cap 即止；头文件如实改写 |
| B6 | P2 | 截断标注覆盖第 200 项真实 id | 已修：保留 199 项真实 + 空 id 哨兵；测试锁定 |
| B7 | P2 | 分页套件头声明契约无用例 | 已修：新增 pager 按钮/过滤重置用例 |
| B8 | P3 | CHANGELOG "19" 与实际 22 不符 | 已修 |
| B9 | P3 | 文档行号/套件名过时 | 已修 |
| B10 | P3 | worker 线程 QPointer 读取（形式 UB） | 接受（同 R1-3，follow-up 在案） |
| B11 | P3 | 缩进错位 | 已修 |

### 结论

P0：0。P1：1（B1）已修。P2：9 全部修复。P3：6 项中 4 修、2 项记录接受。
修复后完整矩阵：29/29 套件全绿（含新增 test_plugin_ui_placement）。
