# R18-D PERFORMANCE

Oracle-C 纪律：本轮 14 项修改全部为**正确性/状态诚实修复**（UI 信号接线、验证分支、修复钳位、导出命名），无算法或数据结构变更；按 DECISIONS#8 未做虚造性能声明。以下是实测的相关量化观察：

## 1. 门控开销（W2/W1a 引入的每事件工作）

- updateEditingUI 现遍历 13（工具栏）+19（菜单）=32 个 QAction 代替 13 个；触发源为 currentLayerChanged/editingStarted/editingStopped（用户节奏事件，非每帧）。
- 实测：test_r14_D 全量（16 用例含 5 个新交互用例、76→340 断言）总墙钟与本轨构建前期观察同量级（无用户可感变化；QAction setEnabled/setProperty 为 O(1) Qt 调用）。
- W6 的 taskUpdated→scheduleRefresh（150ms 去抖）+ WorkbenchStateModel::refresh（singleShot(0) 自合并）：reviewer R2 复核确认无高频热点路径。

## 2. 修复循环收敛（C3）

- RED 实证（修复前）：异构页 MAP_OFF_PAGE 项在 produce 修复循环中永不收敛——每轮 `++applied` 但 rect 不动（290mm 在 210mm 纸上），循环烧满上限（默认 ≤10 轮 re-solve+re-compile+re-preflight，每轮含编译）。
- 修复后：单轮钳位收敛（ledger "applied"，再 preflight 无 code）→ 10 轮×(solve+compile+preflight) 的浪费路径被消除。这是本次唯一的"性能修复"，且以正确性为主、开销消除为副产物。

## 3. 出图确定性（C1）

- 单页选择导出：修复前必然失败（temp 空 + 孤儿文件）；修复后两次同请求 sha256 相等（测试内建断言），命名确定性（selected-page.png 唯一产物）。

## 4. 未测量项（如实声明）

- 大量图层树刷新/项目载入（任务书 D08）：本轮修改不触及这些路径，未做 A/B——避免为凑数测量无关路径。
- sanitizer/race：NOT_EXECUTED（五轨构建资源排满；本轮无新增线程/锁）。
