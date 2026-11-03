# R27-T05 File Ownership

本轨写入（白名单内）：src/workflow、src/experiment、src/grader、src/repair_planner（D1 决策）、
src/processing/framework/task_center.cpp（仅占位符解析语义，D12 决策）、tests/（r27_t05 前缀 +
r18e/r21e/m6/paths/obia/chunk/teaching_admin 等手术式单点）、scripts/build_offline_bundle.sh、
packaging/（appimage checksums + build-appimage pin）、.github/workflows/ci.yml（#1734 单点）、
.planning/r27-t05/**。

共享资源：tests/CMakeLists.txt —— 仅末尾 append-only 块（BEGIN/END R27-T05 标识）；修复的损坏追加区
内容逐 target 与各分支 tip 一致（31 兄弟 target 零删改）。

不动：根 CMakeLists.txt、.gitignore、docs/adr/**、其他轨生产目录、src/agent_loop/**。

## 生成物与共享资源边界声明（PR 显著项）

- `docs/generated/help/*.md`、`data/contracts/contract_graph.snap.json`：#1737 漂移测试的**字节比较受控生成物**，分别经其文档化程序再生成（`--export-help-docs` / `SICNU_CONTRACT_SNAPSHOT_WRITE`）。白名单未列 docs/data，但它们是 tests/** 门禁的直接 fixture——不改则 #1737 无法收口。PR 描述中显著声明，请维护者裁定归属。
- `.gitignore`：仅追加本轨 allow-list 块（先例：每轨各自追加），零重排零删除。
- `src/repair_planner/**`、`src/processing/framework/task_center.cpp`：白名单外的定向修复，依据 DECISIONS D1/D12（里程碑 C 点名 + 无兄弟轨认领），PR 显著声明。
