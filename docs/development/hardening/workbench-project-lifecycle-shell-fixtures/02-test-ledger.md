# Test ledger — Workbench/Project Lifecycle Shell Fixtures

构建纪律：全新 Ninja Debug 构建（ENABLE_TESTS=ON），一次性依赖构建 `ninja -j2 qgis_core qgis_gui`；其余只构建 4 个窄 test target + 受影响 lib（sicnu_qgis_display、sicnu_agent 相关 TU）。**未做全量编译**（sicnu_geo_rs 全链接、其余 2100+ target 均未触碰；组合级验证留给 Prompt 16）。

环境：Linux，offscreen（CTestCustom 全局 `QT_QPA_PLATFORM=offscreen`）；canvas 系测试带 FastExitListener（std::_Exit 绕过 QgsProjContext atexit 崩溃，test_dual_viewport_sync 先例）。

## Target / CASE 清单

| target | CASE 数 | 覆盖 |
|---|---|---|
| test_project_session_boundary | 5 | probe 拒绝（不存在/损坏）不动会话；成功绑定 identity+store；mid-air read 失败回滚（fileName 空、store 关、hook 恰一次）；失败后 Save-As 不碰幻影路径 |
| test_layout_designer_lifecycle | 4 | removeLayout/project.clear()/unmanaged delete 三路退休 + 事后 no-op + 幂等 close + WA_DeleteOnClose 全链退役 |
| test_secondary_map_view_session | 6 | open 全接线；extent 跟随；close 删 sync 复用 widget；**reopen 重建 sync（B2 kill）**+ 新 view id + stats==1/1 防重复；project clear 存活；dtor 释放 engine view |
| test_spatial_tool_registration | 5 | unregister 语义；token arm/release/重复拒绝；move 转移一次释放；in-flight shared_ptr 存活；WorkbenchContextTool 真形接线退役 |

## 执行转录（2026-09-23，全部 offscreen，ninja -j2 窄 target）

| 轮 | 命令 | 结果 |
|---|---|---|
| 首建 | `ninja -j2 qgis_core qgis_gui`（一次性依赖） | exit 0，1823+ objects |
| R2 | build `tests/test_spatial_tool_registration` → 直接运行 | `All tests passed (34 assertions in 5 test cases)` |
| R4 | build+run `tests/test_project_session_boundary` | 首跑 41/41 全过但 exit 139（QgsProjContext atexit，先例已知）→ 加 FastExitListener + 摘要落 stderr → `ALL TESTS PASSED: 41/41 assertions, 5/5 test cases`, exit 0 |
| R3 | build+run `tests/test_layout_designer_lifecycle` | `ALL TESTS PASSED: 18/18 assertions, 4/4 test cases` |
| R5 | build+run `tests/test_secondary_map_view_session` | 修复 fixture（checkable actions / splitter shown / center-contract）后 `ALL TESTS PASSED: 56/56 assertions, 6/6 test cases` |
| 相邻回归 | build+run test_agent_workbench_context / test_dual_viewport_sync / test_edit_session | 17/4 全过、120/15 全过、exit 0 |
| 终门 | `ctest -R "^Session boundary|^Layout designer|^Secondary view session|^SpatialToolRegistry|^RegistrationToken|^WorkbenchContextTool" -j2` × **连续两遍** | PASS1: `100% tests passed, 0 failed out of 20`；PASS2: `100% tests passed, 0 failed out of 20` |

## RED / Sabotage 记录（mutation/adversarial oracle）

1. **B4 master RED**：`git checkout HEAD -- src/app/layout/qgslayoutdesignerdialog.cpp`（master 码）重建重跑 →
   `TESTS FAILED: 13/18 assertions, 0/4 test cases`（:105/:106 removeLayout 路 layout() 非空+窗口仍开、:137 project.clear 路、:158 unmanaged 路、:183 WA_DeleteOnClose 全链未退役）。
   还原修复后 → 18/18 全绿。**即 current-master RED 实证。**
2. **事务回滚 sabotage**：删除 `project.setFileName(QString())` + `closeWorkspaceStore()` 两行 →
   `TESTS FAILED: 40/41 assertions, 4/5 test cases`（恰击中 `read failure rolls back` CASE）。还原后 41/41 全绿。
3. **B2 sabotage**：sync 控制器创建塞回 ensureWidget（= master 首建分支行为等价）→
   `TESTS FAILED: 42/44 assertions, 4/6 test cases`（恰击中 reopen 后 `syncController() != nullptr` 与 project-clear 后跟随两处）。还原后 56/56 全绿。

Sabotage 全部还原（tree 内 `SABOTAGE` 计 0）；`git diff --check` 通过。

## 独立 Review round 1（adversarial reviewer）与修复

Reviewer 结论：PROCEED-WITH-FIXES，无 P0（其自建 6 个对抗二进制 + 4 个 sabotage 复现，6 场景全过：双 reopen、pending-throttle 销毁、真读语义失败+重试、designer 跨 pending 输入/级联退役、in-flight token）。

- **P1（oracle 盲区，已修）**：注入 readFn 原先不设 fileName，与 `QgsProject::read` 真实失败签名（先赋值后失败）不符 → 仅删 `setFileName` 回滚行时套件仍 41/41 全过。修复：两个 ReadFailed 用例的 readFn 改为 `p.setFileName(path); return false;`。**强化后单独 sabotage 该行 → 35/37、3/5 cases 红**（:204/:232 两个幻影 identity 断言齐杀），还原 41/41 绿。
- **P2（已修）**：ReadFailed 分支补 `updateEditingUI(nullptr)`，与"mirror newProject"注释一致（否则编辑动作在空会话上残留启用）。
- **P3-3（已修）**：失败分支 `m_mapCanvas` 访问加守卫，与前置 settle 守卫一致。
- P3 其余（settle 时序前移、governance 提示在 ReadFailed 抑制、ProbeFailed diagnostics 未渲染、session 持裸 ProjectContext* 的隐式不变式、FastExitListener 权衡）：逐条记录于 01-status-matrix/PR 正文，不改（与 master 同 UX 或属先例权衡）。
- Review 后终门：两遍 ctest `100% passed, 0 failed out of 20`。
