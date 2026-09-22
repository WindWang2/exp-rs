# Recon — integration-build-contract-drift (hardening 01/20)

Baseline: `origin/master` = `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01` (merge of #1236),
fetched 2026-09-22. Open PRs at start: **#1237 only** (`feat/undergrad-lab-cockpit`) —
owns `src/teaching/**`, `src/app/teaching/**` and touches root `CMakeLists.txt`,
`src/app/CMakeLists.txt`, `tests/CMakeLists.txt`. This track keeps its deltas to those
shared files contiguous and semantically union-able. Open issues: 0.

## 现状矩阵（orphaned / unwired modules）

Mechanical inventory over every `src/**/CMakeLists.txt` vs root `add_subdirectory`
graph vs `tests/CMakeLists.txt` registrations. Confirmed with
`git log -S` that none of these were ever wired (not a regression, a never-landed
second half of merged PRs). CI could not catch it: the last 8 `master` CI runs are
`completed/cancelled` (1–5 min) or `queued` >1 h, so merges proceeded without a green
build. Reproduced locally: `cmake --build build-dev --target test_agent_loop_core`
fails twice — `fatal error: agent_loop/session_state.h: No such file or directory`
and `cannot find -lsicnu_agent_loop` — so the **default `cmake --build` (target
`all`) is broken on master**.

| 模块 | 定义 target | 源文件 | 接线状态 | 测试源 (tests/) | 测试注册 | 消费者 (src/) | PR |
|---|---|---|---|---|---|---|---|
| `src/verify` (ADR 0172 Slice A) | `sicnu_verifier` | 4 cpp | CMakeLists 存在，无任何 `add_subdirectory` | `test_verifier_schema.cpp` | ❌ 未注册 | 无 | #1191 |
| `src/repair_planner` (RS14-03, ADR 0174) | `sicnu_repair_planner` | 2 cpp | 同上 | `test_repair_planner_schema.cpp` | ❌ 未注册 | 无 | #1194 |
| `src/agent_loop` (RS14-11, ADR 0175) | `sicnu_agent_loop` | 6 cpp | 同上 | `test_agent_loop_{core,seams,modes,e2e,resume}.cpp` | ⚠️ 5 个 target 已注册，链接幻影 target（link 破坏 `all`） | 无（注释宣称的生产 adapter `src/agent/tools/agent_session_adapter.*` 在 master 不存在） | #1201 |
| `src/recipes` (RS14-20, #1206) | — 无 CMakeLists | 9 cpp + 9 h | 目录完全未接线 | `test_recipe_{compiler,equivalence,lookup,registry,schema,validator}.cpp`, `test_lab_document.cpp`, `test_lab_source.cpp` | ❌ 8 个全部未注册 | 无 | #1206 |
| `src/preflight` (RS14-02) | `sicnu_preflight` | 3 cpp | CMakeLists 存在，无 `add_subdirectory` | `test_preflight_report_schema.cpp` | ❌ 未注册（`test_preflight.cpp` 是旧的 processing-framework 测试，非本模块） | 无 | #1207 |
| `src/study/bridge` (RS14-07) | `sicnu_study_bridge` | 1 cpp (qt) | CMakeLists 存在，无 `add_subdirectory`（root 只加 `src/study`；`src/study/CMakeLists.txt` 也不加 bridge） | `test_study_e2e.cpp` | ❌ 未注册 | 无（opt-in 设计） | #1205 系列 |

关键事实：

1. 全树 `grep add_subdirectory` 无一条指向这 6 个目录；`git log -S "src/verify" -- CMakeLists.txt`
   等为空 → 从未接线。
2. `tests/CMakeLists.txt` 中 5 个 `test_agent_loop_*` 用裸 `add_executable` +
   `target_link_libraries(... sicnu_agent_loop)`，target 未定义 → CMake 把它当
   `-lsicnu_agent_loop` 传给链接器，configure 静默通过，`all` 构建必炸。
3. 11 个测试源文件完全未注册（silent test gap）：`test_verifier_schema`、
   `test_repair_planner_schema`、`test_preflight_report_schema`、
   `test_recipe_*`×6、`test_lab_document`、`test_lab_source`；加上 `test_study_e2e`
   共 12 个。
4. 依赖图：verify/preflight/repair_planner/recipes 是自包含叶子（仅 jsoncpp）；
   `study/bridge` 依赖 `Sicnu::study` + `sicnu_task_center`（qt_add_library）；
   agent_loop 仅 jsoncpp。src/ 生产代码零 include 这些头 → 补接线不会改变生产行为。
5. `capability_state_graph`（seed 线索）：全树零匹配，不存在该模块/文件 → 线索不成立，记录后关闭。
6. `src/python/*.cpp`（api/runner）由 tests 直编、console 由 src/app 条件编译
   （`SICNU_EMBED_PYTHON`，默认 OFF）→ 有意设计，非 drift。

## 修复方案（最小接线，不创造新模块）

- root `CMakeLists.txt`：在 `add_subdirectory(src/experiment/debugger)` 后补一个
  连续块，按既有注释风格加 6 条 `add_subdirectory`。
- `src/recipes/CMakeLists.txt`：新建，`sicnu_recipes` STATIC 叶子，照抄
  `sicnu_grader`/`sicnu_verifier` 的 jsoncpp 解析块。
- `tests/CMakeLists.txt`：注册 19 个缺失测试。纯叶子测试用既有 RS14 裸
  `add_executable` + `Catch2::Catch2WithMain` + 模块库模式（模块 PUBLIC jsoncpp
  传递提供）；`test_study_e2e` 用 `sicnu_add_test` + `sicnu_study_bridge`。
- 结构 oracle（新测试 `test_build_wiring_drift`）：读仓库文件（排除
  `build*`/`.git`/`CMakeFiles`/`Testing`，保证密闭性），机械断言
  (a) 每个 `src/**/CMakeLists.txt` 从 root 可经 `add_subdirectory` 文本边到达
  （含变量/绝对路径的边跳过——fail-open 方向）；
  (b) 每个 `src/**/*.cpp` 以 `*.cpp` token 出现在"合格"脚本中：脚本位于该文件
  在 src/ 内的任一祖先目录下（本模块自己的 CMakeLists），或在 src/ 之外
  （tests/tools 直编）。同名文件在兄弟模块中不能互相遮蔽；
  (c) `tests/test_*.cpp`（递归）的 stem 以整词出现在 `tests/CMakeLists.txt`。
  已知 fail-open 限制：注释行中的文件名/stem 仍算"已注册"。
  杀伤力：撤掉本 PR 的任一模块接线、任一源文件列表项或任一测试注册
  （sabotage/mutation），oracle 必红并列出 offender。

## 追加发现（inventory 扩展）

7. `tests/test_sci_inspector.cpp`（RS14-19 #1205）未注册，且其唯一被测源
   `src/app/workbench/scientific/sci_inspection.cpp` 不被任何 target 编译。
   修复：测试直编该 .cpp（先例：`test_interaction_tools` 直编
   `../src/agent/mcp_server.cpp`），不动 #1237/#1238/#1239 都在改的
   `src/app/CMakeLists.txt`。
8. `src/gui/codeeditors/qscilexer_stubs_moc.cpp` 是死文件：其注释宣称的
   AUTOMOC 触发机制已被 `src/gui/CMakeLists.txt`（~166-182 行）直接把 stub 头
   列为 target 源的做法取代；全树零引用 → 从未参与任何构建。删除（link 中性：
   其符号今天就不存在）。
9. 未注册测试全量复查（词边界匹配，815 个 `tests/test_*.cpp`）：除矩阵所列
   12 个外还有 6 个：`test_curriculum`（RS14-18）、`test_explain_schema`
   （RS14-15）、`test_faultlab`（RS14-13）、`test_grader_schema`（RS14-05）、
   `test_lab_runtime`（LabSpec v3）、`test_sci_inspector`。共 18 个（+oracle
   本体 19 个新 target）。其余 797 个均已注册。
10. ADR 编号重复（0130×8、0166×4、0174×2 等）自 milestone-4 起就是仓库惯例
    （编号按里程碑批次簇状复用），非 union merge 损伤 → 不重编号（重编号会
    大规模破坏代码注释/文档 citation），记录为惯例。
11. `capability_state_graph`（seed 线索）：全树零匹配，不存在 → 线索不成立。
12. census 字节门快照 `data/contracts/determinism_census.snap.json` 只覆盖
    operator determinism census，不含构建 target/test 注册 → 本接线改动不触碰。

## 并发冲突复查（PR 前 + slice 间）

启动时 open PR 仅 #1237；slice 完成前复查新增 #1238（experiment-studio）、
#1239（teaching-admin）、#1240（science-context）。四者都改 root/tests
CMakeLists，但 #1240 的插入点在 `scientific_state` 之后、#1237/#1238/#1239
在各自模块区域；本 PR 的两处 tests/CMakeLists 改动（agent_loop_resume 块后
连续 hunk）与 root CMake 单一连续块保持最小、可语义 union。

## 未做事项分类

- 已被其他 Track 拥有：`src/teaching/**`、`src/app/teaching/**`、#1237 的 shell 接线。
- 无法复现：`capability_state_graph`（master 无此物）。
- 需要真实平台环境：线上 CI runner 取消/排队问题（host 端，本 PR 无法修复）。
- 明确未来方向（不做）：`src/agent_loop` 注释中的 Qt 生产 adapter
  （`src/agent/tools/agent_session_adapter.*`）、verify 的 harness/exp-prov/workflow
  adapters —— 均为注释里声明的 future adapters，属产品方向，不实现。
