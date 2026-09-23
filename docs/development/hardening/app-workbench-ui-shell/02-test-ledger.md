# Test Ledger — hardening/app-workbench-ui-shell

构建纪律：`-j2`（`source ~/pwb-sdks/env.sh && cmake --build build --parallel 2 --target <target>`）。
测试运行：ctest -R 或直接运行二进制（Catch2）。

## Oracle 清单

| # | 缺陷 | Oracle | 杀伤力证据 |
|---|---|---|---|
| O1 | GW1 跨实验状态污染（Next 宣告未开始的实验完成） | `test_guided_workflow_widget` "Selecting another workflow ends the active session"（真实 widget offscreen，SICNU_DATA_DIR 注入 labs，真实 QListWidget 选择 + meta-object 调槽） | 待填 RED |
| O2 | GW1 步索引越界（Run This Step 越过新实验步数 → QList OOB，Debug=Q_ASSERT abort） | `test_guided_workflow_widget` "Run This Step after switching workflows never indexes past the steps" | 待填 RED |
| O3 | B5 CommandRegistry unregister 释放快捷键保留失败 → reload 被拒 | `test_command_registry` "unregistering a shortcut-carrying command frees its canonical key" + "keeps reservations of commands outside the prefix" | 待填 RED |
| O4 | B1/B3 的 store 层依赖契约（边界重置 → 空 runtime 保存为新 mission；poisoned 拒绝保存） | `test_mission_runtime_store`（4 CASE） | 新 harness；GREEN 后以 sabotage 验证守卫 |
| O5 | B1 窗口层（newProject/openProject 清 mission） | 无完整 shell fixture（明确记录）；实现为 4 行状态重置 + store 契约 O4 固化其可观察终态 | fixture = 后续 slice |

## RED 证据（旧实现上运行）

`build/tests/test_guided_workflow_widget "[boundary]"`（QT_QPA_PLATFORM=offscreen，UNFIXED master 源码编译，2026-09-23）：

```
Selecting another workflow ends the active session
tests/test_guided_workflow_widget.cpp:367: FAILED:
  REQUIRE( completedCount == 0 )  with expansion:  1 == 0
  → 从未开始的 lab92_boundary_b 被宣告 workflowCompleted（状态跨实验泄漏实证）

Run This Step after switching workflows never indexes past the steps
tests/test_guided_workflow_widget.cpp:370: FAILED: ... due to a fatal error condition:
  SIGABRT - Abort (abnormal termination) signal
  → 陈旧步索引对 QList<WorkflowStep> 越界读，Debug Q_ASSERT abort（release=UB）
```

test cases: 2 | 0 passed | 2 failed。

## GREEN + 双遍验证

`cmake --build build --parallel 2 --target test_guided_workflow_widget test_command_registry test_mission_runtime_store` → exit 0，offscreen 运行：

| 二进制 | 结果 |
|---|---|
| `test_guided_workflow_widget`（全部 9 CASE） | All tests passed (103 assertions) — 含原 RED 两 CASE 转绿、Shipped labs（master 上原本已红）转绿、version-scoped steps、stepless-lab 诚实呈现 |
| `test_command_registry`（11 CASE） | All tests passed (57 assertions) |
| `test_mission_runtime_store`（4 CASE，新 harness） | All tests passed (36 assertions) |

窗口 TU（main_window.cpp / main_window_project.cpp 的 Slice C + B8 编辑）以 compile_commands 的真实编译参数 `-fsyntax-only` 编译通过（sicnu_geo_rs 整链接留给 review 前的最终 targeted build）。

第二遍验证（双遍 oracle）：review 修复后最终两遍全绿 —— 12 个 targeted 套件 × 2 passes，0 失败（/tmp/double_pass2.log）：test_guided_workflow_widget(112/10)、test_labspec(146/10)、test_command_registry(57/11)、test_mission_runtime_store(36/4)、test_guided_workflow_sync(41/6)、test_workbench_host(50/8)、test_selection_context(60/13)、test_inspector_host(27/6)、test_sci_inspector(63/5，首次运行)、test_workbench_state_model(29/6)、test_workbench_shutdown_policy(54/4)、test_workbench_enum_provider(28/5)。

改动 TU 无新增 warning（build_re.log / build_green4.log / build_review_fixes.log 中无指向被改文件的 warning）。

## Sabotage / mutation

| 实验 | 方法 | 结果 |
|---|---|---|
| loader 版本化 steps 修复 | `git stash push -- src/app/widgets/lab_spec_loader.cpp` → 重建 → `[labspec]` | 5 CASE 中 2 FAILED（Shipped labs + version-scoped）→ 恢复后 5/5 passed。**oracle 杀伤力实证** |
| registry 快捷键释放修复 | `git stash push -- src/app/workbench/command_registry.cpp` → 重建 → `[reload]` | 2/2 新 CASE FAILED → 恢复后 passed。**oracle 杀伤力实证** |
| GW1 会话边界 | 无需 sabotage：RED 阶段直接在旧实现上取得 assertion 失败 + SIGABRT | 见上 |

## 独立对抗 review（round 1）与关闭记录

Reviewer 结论 READY（无 P0/P1）。修复的发现：
- [P2] 正向控制缺失 → 新增 "Positive control: a walkable lab completes end to end"（started==1、stepCompleted 0/1/2、completed==1）。
- [P2] 跨工作流后 restoreButton else 分支强制禁用 + 完成消息跨边界渗透 → else-if(m_workflowActive) 分支改为对新会话 updateStepDisplay()；消息仅在 owning workflow 内展示（Task Center 仍留全局记录）。
- [P3] 错误条目选中绕过会话重置 → 错误分支同样复位会话状态。
- [P3] onMissionSidecarChanged 未实现注释声称的路径守卫 → 增加 `path != missionSidecarPathForProject(current)` 即 return。
- [P3] ledger 待填段 → 本节即补齐。

记录不修（按分类）：
- 已被其他 lane 拥有/预存：data/schemas 与 data/labs 两份 labspec.schema.json 本就互相不一致且均未声明 steps 的版本化语义（无任何可执行校验消费它们）；recipe compiler 将 lab12 形态判为 no_steps Error（tests/test_recipe_compiler.cpp:226-242，fail-closed，语义不同不属 UI shell 裁决）。
- 相邻预存（不同 slice）：openProject 二次 read 失败后 QgsProject::fileName() 仍指向目标文件（图层文件面；mission 面本轮已闭环）。
- widget 测试 target 的 CMAKE_SOURCE_DIR define 惰性（无害，SICNU_DATA_DIR 才是 ScopedLabDir 的实际机制）。

## Sabotage / mutation

待填。
