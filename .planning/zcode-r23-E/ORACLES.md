# ORACLES — R23-E 逐项交付 Oracle 状态

- [x] **O0** 新 master/PR/Issues/worktree 基线+重检：BASELINE.md §0–§2（双侧 fetch 复核、Open PR=0、Issues=144、worktree 独立、Windows 主 checkout 零写入）。
- [~] **O1** DONE 声明均带生产调用链与 RED→GREEN×2：WP2（5 用例 ×3 次全绿 + 2 突变 RED）；WP3（7 场景验证 + 判别腿）；WP1/WP5 待构建完成后补（构建进行中）；WP4 脚本就绪待跑。DEFERRED：WP8（升级/卸载 canary — 见 TASKS）。
- [~] **O2** 反例/失败不伪造成功：WP1 类型门（恶意类型→typed 拒绝 + worker 存活）；WP2 typed -32000/-32602/-32603；T3 cmd 守卫 5 恶意值 + canary。
- [ ] **O3** 性能对比：PERF_BASELINE.md 记 N/A 理由（本轨无热路径改动；测试新增为冷路径）。待最终填写。
- [~] **O4** 本地 build/CTest + 真实用户路径 smoke：基线构建进行中；install smoke 脚本就绪（w15）；Windows .cmd 真机腿已完成（T3）；macOS=NOT_EXECUTED，MSVC=NOT_EXECUTED。
- [ ] **O5** 两轮独立 reviewer：待实现冻结后执行（R1/R2 fresh subagent，只给 diff+契约）。
- [~] **O6** 白名单：OWNERSHIP.md 声明；改动清单=tests/（3 新测试+共享注册块 append-only）、scripts/（1 新脚本）、.planning/**；生产源码零触碰（见下）。注册并集门已自跑（live clean + 1038 计数）。
- [ ] **O7** 文档/测试/实现一致、无 mock-only、无 TODO 遗留：终检清单（FINAL_REPORT 前逐项）。
- [ ] **O8** push + 非 draft PR（base=master）+ PR_BODY：待 review 闭环后。

## 生产源码改动 = 0（重要事实）

本轨全部交付为「测试 + 工程脚本 + 门禁」，未修改任何 src/** 生产实现（#1662 sweep 的 E 域修复经核实已在 master 且质量合格，本轨为其补回归防线而非重写）。WP4 若发现 install 缺口将按最小修复并更新此处。

## 变更文件全录（含未提交）

- tests/test_r23_e_plugin_typed_gates.cpp（新增，WP1）
- tests/test_r23_e_python_worker_typed.py（新增，WP2，已提交 b39942cd）
- tests/test_r23_e_native_terminal.cpp（新增，WP5）
- scripts/check_merge_registration_integrity.py（新增，WP3，已提交 11497c6c）
- tests/CMakeLists.txt（append-only 注册块 ×4，其中 WP2/WP3 块已提交）
- .planning/zcode-r23-E/**（账本）
