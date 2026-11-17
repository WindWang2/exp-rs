# REVIEW_R1 — R32-T05

- 审查对象：`53412c86..58083356`（5 commit）
- 审查者：独立子 agent（未参与实现），**实际用本树 compile_commands.json 做了语法编译**（GCC 15/Qt 6.12）
- 日期：2026-11-17

## 发现与处置

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| 1 | QPointer 不完整类型实例化：dock `m_dataManager = dataManager` 与两个 service 头的内联访问器在 8 个 TU 产生 invalid static_cast（sicnu_agent/应用/6个测试目标无法编译） | **P0 BLOCKER** | 已修（`1fc381bf28`）：dock .cpp 补 `data/data_manager.h`；两 service 访问器移至 .cpp；修复后 7 个原失败 TU + 全部改动 TU 用 compile DB `-fsyntax-only` 复验 OK |
| 2 | 新测试 `capabilitiesForRequirementKind` override 名错（基类为 `capabilitiesForRequirement`），抽象类无法实例化 | **BLOCKER** | 已修（同 commit）；语法编译 OK |
| 3 | plugin_snapshot.cpp 两处把 `fs::path` 型 `relative` 误包进 `pathFromUtf8`：GCC 经 `operator const char*` 窄化编译通过，MSVC（#2111 目标平台）编译失败 | **P1** | 已修（回退为 `path / relative`）；GCC 语法复验 OK |
| 4 | 拒绝 rebind 分支未更新 AgentServices/InteractionToolRegistry 的旧项目 raw 指针（既有缺口，非本变更引入） | P2 | 部分修复（同 commit）：AgentServices 与两 service 镜像新 owner（QPointer 守卫下安全降级）；InteractionToolRegistry 重注册语义复杂，未扩大范围——记录为已知残差 |
| 5 | sendPrompt 的 catalog-error/Pi-gate 拒绝路径不恢复输入文本（既有行为，非本变更引入） | P3 | 不改（超出 #2073 范围，避免 churn）；记录 |
| 6 | clear-during-stream + pending identity flag 使 finishRunUi 连续两次 createConversationRuntime（无害重复工作） | P3 | 不改（正确性无影响）；记录 |
| 7 | #2078 差异腿无公共可观察面（run() 的 post-hoc 诊断恒 non-repairable） | 观察 | 与 DECISIONS D7 一致；行为测试钉住可观察不变量 |

## 各 issue 判定（reviewer 结论）

- OK（含证据）：#2076、#2077、#2078、#2079、#2080、#2106、#2107、#2109、#2112、#2073（行为）、#2075、#2081（fixture 行为，受 #1 阻塞）
- 修复后需复验：#2074（#1/#4）、#2111（#3）、新测试编译（#2）
- 测试风险评估：新 test_pi_process 用例无挂起风险（bounded until）、真实区分修复行为；dock 测试的同步断言依赖 native runtime 的同步 ready 发射（成立）
- 范围违规：无

## 结论

修复 BLOCKER×2 + P1×1 + P2 部分（commit `1fc381bf28`），全部改动 TU 经 compile DB 语法编译通过。剩余 P3 记录不改。R2 将绑定最终 HEAD 复查。
