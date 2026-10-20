# R19-02 ORACLES — 逐行客观验收（终版）

基线 BASE_SHA=`e12a9afbc8883a474dc9ffcaf3345ae55863dc71`。工作树 `/home/kevin/project/exp-rs-02-pi-mcp-workflow-20261020T102409Z`，分支 `agent/r19-02-pi-mcp-workflow-20261020T102409Z`。
运行环境（所有本地验证统一）：`LD_LIBRARY_PATH=<build>/link-shim:<vendor lib> QT_QPA_PLATFORM=offscreen PROJ_DATA=<vendor>/usr/share/proj`；构建经全局锁 -j2。

| ID | 触发源/风险 | 测试/探针命令 | baseline RED | fix commit | GREEN-1 | GREEN-2 | negative | reviewer | post-rebase |
|---|---|---|---|---|---|---|---|---|---|
| O-T3 | B13 merge-loss 静默回滚（两起已证实事故） | `python3 -m unittest discover -s scripts/dev/tests -t scripts/dev -p 'test_merge_loss_audit.py'`；真实史 `python3 scripts/dev/merge_loss_audit.py --json`（29596c913=30 路径、5e14bd1d7b=6 路径必报） | 工具不存在 | c79c5c60d（+R1 修复 14057e801） | OK 7/7 | OK 7/7 | clean/conflict/exit-2/拒收 4 组负向 + 真实史双事故正反校准 | R1 READY（P3×3 已修） | 待 push 前 |
| O-T0 | #1505-P1 修复被 5e14bd1d7b 静默回滚（检测器新发现） | `test_science_context_broker "capability facts snapshot…"`；`tests/test_science_context_concurrency "TC_R16B_01*"` | 实测 RED×2：`revision()==9 where 7 expected`；churn `1==600` coherent | a29f935e0 | broker 40例/183断言 PASS；concurrency 14例/10130断言 PASS | 同 run2 全 PASS | 未修 src 上跑恢复测试=RED 即负向证明（已执行） | R1 READY（RED 预言与实测一致） | 待 |
| O-T1 | #1654 残余：requestCancel store 与 runStep consume 无全序 | `./build-r19-02/tests/test_r19_02_workflow_cancel_order` + 回归 `test_workflow_cancel` | 代码检视证明（两 store 均锁外无全序）；测试为行为网（头注释声明） | dce4cb180 | 4例/1009断言 PASS；回归 8例/74断言 PASS | 同 | (a)(b)(d) 为新旧行为共同护栏（防回归）；(c) 200 轮并发栅栏 | R1 READY（P2 记录：全序靠检视，见 REVIEW_R1） | 待 |
| O-T2 | #1636 explain guidance_ 死成员 | `test_explain_{validator,guidance_coverage,builder}` | grep 零读取点（静态 RED）；2 参构造编译门 | 75ba7ad94 | 11/21、4/218、13/70 全 PASS | 同 | 编译失败即捕获 API 误删 | R1 READY | 待 |
| O-T6 | pi knowledge 页超预算（33384>32768） | `cd pi && node --test test/scientific_workflow_compiler_11.test.mjs`；`capability_knowledge_tool gen-pages --check` | 实测 RED（39 中 1 败） | 7944fa75a | 5/5 PASS；page 32521B；--check zero diff | 同 | 预算测试本身即守护 | R1 READY（语义无损逐条核对） | 待 |
| O-T7 | master 快照与 master 代码漂移（mosaic r12-a 文案 + workspace/cartography 规范化互斥） | `test_surface_snapshot` 三腿 | 实测 RED：projection/wire 两腿 mosaic 失败 | 3ff4e0ba4 | projection+wire 腿全绿；registry 腙 workspace 全绿 | 同 | 失败信息含工具名+regen 提示（自诊断） | R2 审查 | 待 |
| O-BASE | 全族回归 | 14 套 ×2（见 TEST_RESULTS） | — | — | 11 全绿×2；parity/e2e 补建二进制后全绿×2 | snapshot 1 例=CT-6 红区 | — | R1/R2 | 待 |
| O-PI | pi 全套 | `node --test test/*.test.mjs` ×2 | 38/39（T6 RED） | 7944fa75a | 39/39 | 39/39 | — | R1 | 待 |

## KNOWN_BASELINE_FAIL / NOT_EXECUTED
- KNOWN_BASELINE_FAIL: test_surface_snapshot registry 腿 × cartography 7 工具（CT-6 R2 修正版，轨 03 红区：catalog_index/chart_list/list_rules/list_token_sets 缺 `properties:{}`；diff_templates/export/explain 缺 `type:"object"`；lint_catalog 已 canonical 通过。REQUIRE 首个不匹配中止故单轮显示 1 败）。本轨已把同型的 workspace:undo/redo 修复并在快照中给出权威形态。
- NOT_EXECUTED: CI（#1491 全仓失效）；Windows/macOS lane（无宿主）；plugin_host 全进程族（本轮目标集未含，留 R2 后跟进轮）。

## 完成定义核查（PR 时逐项打钩）
- [ ] `git diff --check` clean
- [ ] `git diff origin/master...HEAD --name-only` 全部在 OWNERSHIP 内（tests/CMakeLists.txt append-only）
- [ ] 无伪造恒真测试（T0 双 RED 实测；T1 行为网+检视声明；T3 真实史双向校准）
- [ ] NOT_EXECUTED 如实标注
- [ ] R1（READY）+ R2 P0/P1 全闭环
- [ ] 基线故障与排除已记录（TEST_RESULTS.md）
- [ ] PR 已创建：`hardening(r19-02): Pi MCP harness, workflow and plugin contract closure`
