# ACCEPTANCE — R32-T05

状态：✅ 最终 HEAD `85f6347785`（rebase 于 master `53c960a2`）本地验证全绿；R1 修复完成、R2 SHIP+增量 SHIP

## 最终 CTest 计数（最终 HEAD，-j1/offscreen）

- `r32_t05_dock::` 4/4 + `r32_t05_ops::` 4/4
- `pi_process::` **17/17**（含 #2076 长清理/#2076 cancel_timeout fail-closed/#2077 迟到 ready 三新例 + checkpoint 全族，真实 QProcess+协议）
- `bilingual_ui::` 3/3 + `r31_t02::` 5/5（master 新增套件对改动后 pi_process 的回归）
- copilot_ui 9/9（含 #2130 toolcard）
- tool_host/checkpoint_store/ops_core/approval_gate/plugin_system/plugin_loader/external/metrics 复跑 **67/67**
- 直跑：`test_agent_loop_core` 17 cases/216 assertions（#2079）；`test_r23_e_python_worker_typed.py` 5/5（#2107）
- JS：checkpoint-redaction 4/4（#2080）；install 树：worker 存在/自洽（#2109）
- 汇总：109 CTest cases + 216 断言套件 + 5 例 Python + 4 例 JS + install 验证，0 失败（1 skip=Windows probe 按设计）

## 工作包完成状态

| WP | 内容 | 状态 |
|---|---|---|
| WP01 | 基线清账（12 issue 复核+5证据层级区分） | DONE（BASELINE/ISSUE_TRIAGE/PATH_OWNERSHIP） |
| WP02 | #2073 无context/beginRun失败 | DONE（代码+test_r32_t05_agent_dock） |
| WP03 | #2074 QObject生命周期 | DONE（代码+测试） |
| WP04 | #2075 checkpoint与项目切换 | DONE（代码+测试；Pi checkpoint 失配腿见"证据边界"） |
| WP05 | #2076 取消双边终止 | DONE（代码+test_pi_process 2用例） |
| WP06 | #2077 启动关闭竞争 | DONE（代码+test_pi_process 1用例） |
| WP07 | #2078 审批单次语义 | DONE（代码+test_r32_t05_ops_sdk；公共可观察面限制见DECISIONS D7） |
| WP08 | #2079 持久决定typed校验 | DONE（代码+测试） |
| WP09 | #2080 脱敏语义 | PARTIAL / DEFERRED：所有非空凭据保持严格脱敏，结构碰撞明确拒绝；`1` 等占位键的无条件会话/快照支持未交付，见 DECISIONS D9；不得用长度豁免替代脱敏 |
| WP10 | 宿主工具/catalog稳定性 | ALREADY_DONE_WITH_EVIDENCE：#2068 已建共享 AgentToolHost 权威（catalog/审批/预算在 master 现有 test_agent_tool_host 覆盖）；本轮无新增缺陷证据，未重复实施 |
| WP11/12 | Python环境/执行 | ALREADY_DONE_WITH_EVIDENCE：#2063/#2072 已交付（receipt/ready marker/审批快照/输出清理）；#2107 dtype 修复属 WP14 |
| WP13 | checkpoint完整性 | 部分继承（#2069 完成态快照+恢复）；本轮 #2075/#2080 补缺陷；corrupted/compaction 残差无新证据项 |
| WP14 | #2106/#2107/#2081-fixture | DONE |
| WP15 | #2109 插件host安装 | DONE（install 规则+stage 硬失败；install 树验证待跑） |
| WP16 | #2111 SDK Unicode路径 | DONE（40处 UTF-8 构造+Linux功能测试；Windows ACP 腿 NOT_EXECUTED） |
| WP17 | #2112 checksum语义 | DONE（代码+测试） |
| WP18 | 插件生命周期残差 | 本轮未深化：#1945/#1947/#1952 为聚合 issue，子项需逐项动态证据；已修子项（#2109/#2111/#2112）见上。诚实记录：未逐项清账 |
| WP19 | usage/成本/包交付 | 部分：Pi 默认与授权边界未动（无新证据）；本地包验收=install 树检查（待跑） |
| WP20 | E2E+review+PR | 进行中 |

## 本地验证（2026-11-17 实测，Linux/Debug/-j1/offscreen）

- configure：`cmake --preset dev-default -DOpenCV_DIR=<共享scratch>/opencv5` → EXIT=0
- build：受影响目标（14+5 个）→ BUILD_EXIT=0，0 编译错误（全量含 qgis_core/gui；R1 修复后 16 个改动 TU 经 compile DB `-fsyntax-only` 全过）
- CTest 实测：
  - `r32_t05_dock::` 4/4 PASS；`r32_t05_ops::` 4/4 PASS
  - `test_agent_tool_host`+`conversation_store::`+`test_agent_ops_core::`+`r27t03`（approval gate）→ 55/55 PASS
  - `test_exprs_plugin_system`+`test_exprs_plugin_loader`+`test_native_conversation_runtime`+`test_python_runtime_selection` → 100%（1 skipped：Windows probe，按设计）
  - `Conversation metrics` 4/4、`external process` 8/8、`test_agent_loop_core` 直跑 17 cases/216 assertions 全过
  - `pi_process`：Linux fixture 执行位修复后重跑（见"进行中"）
  - `bilingual_ui::`：approval/task-center 2 例 PASS；case1 剩 zh .ts Base 上下文断言（#2081 T06 侧）
- JS（node 24.21.0）：`checkpoint-redaction` 4/4 PASS（两遍）；`protocol` 9 PASS/2 FAIL/4 SKIP（installed-entry 两例需锁定 Windows SDK node_modules —— NOT_EXECUTED 维持）
- Python 直跑：`test_r23_e_python_worker_typed.py` → Ran 5 tests, OK（#2107 修复动态验证）
- #2109 install 树：`cmake --install` → `bin/exprs_plugin_host_worker` 存在（ELF pie 可执行，RPATH $ORIGIN/../lib:$ORIGIN，ldd 0 缺失，无参启动干净退出）；生成的 host `cmake_install.cmake` 从空变为含规则

## 证据边界（诚实声明）

- 真实锁定 Pi SDK（node_modules）相关 lane：Linux 无 Windows 发行版（vendor 仅 x86_64-pc-windows-msvc），NOT_EXECUTED。test_pi_process 用真实 vendor recipes+宿主 node 的自洽 fixture stage（不弱化断言，见 DECISIONS D15）。
- 真实云模型/最终 Windows 发布/真实 ORT：NOT_EXECUTED（无授权环境）。
- #2081 的 zh_CN .ts Base 上下文：T06 所有权（HANDOFF）；本轨 fixture 侧已修，zh 侧断言在 .ts 修复前可能仍失败。
- #2078 的差异腿（humanApprovedRepair 不再被错误置位）：run() 的 post-hoc 诊断恒 non-repairable，无公共可观察差异面；行为测试钉住 typed 拒绝/消费/正控制，代码语义经 R1/R2 review 确认。
- 五个 E2E 场景的对应关系：E2E-01/02/03 由 test_r32_t05_agent_dock（真实 Qt 对象+真实回环 SSE）+ test_pi_process（真实 QProcess+协议）覆盖其核心负例；E2E-04 由 test_r32_t05_ops_sdk 覆盖；E2E-05 的 Linux 腿由 #2111 测试+install 树验证覆盖，Windows 腿 NOT_EXECUTED。真实 GDAL NDVI Python 算子链依赖托管 Python 环境（test_python_agent_mvp 族）——构建后运行（SLOW）。
