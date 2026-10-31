# REVIEW R1（独立子代理，未参与实现；2026-10-30）
评审范围：git show HEAD（当时 1 commit f433fc88bf）。结论：**无 P0/P1**。
| 级别 | 发现 | 处置 |
|---|---|---|
| P2 | run_loop.cpp:321 diagnose 面用 readVerificationEvidence（Corrupt→nullopt）→ harness:diagnose_run 对损坏证据报 completed/无 failures，把"成功"结论交给修复循环 | 已修（b714b2cf7）：run_loop 改 inspectVerificationEvidence，Corrupt → verification_evidence_integrity failure 条目 |
| P2 | evidence.cpp ForeignRun 把"run 块缺失/不可归属"并入他 run → 重算后覆盖，销毁审计轨迹 | 已修（b714b2cf7）：recordedRun 空 → Corrupt；oracle A3b |
| P2 | provenance exists-then-write 竞态窗口仍在（rename 无 NOREPLACE；引擎 writer 并发罕见） | 取舍：窗口窄（harness 仅在 persistEvidence 路径、引擎仅在 step 完成时写）、最坏后果自愈（下次 persist 修正）；记录于 RISK_REGISTER，不引入 renameat2 跨平台改动 |
| P3 | currentRun 空静默不替换（不可达：WorkflowRun 拒绝空 run id） | 已加 fail-closed 注释；不补日志（无生产可达路径） |
| P3 | Corrupt entry 键形不一致 | 已修（b714b2cf7）补同形 null 键；R2 再改为真实探测 |
| P3 | readRawFile constData() NUL 截断 | 已修：size()-based 读取 |
| P1* | capability familyMap 与签入 sidecar 分歧（validateEntry 强制 canonical==family → loadProblems → 3 个 gate 失败） | 已修（b714b2cf7）：4 sidecar family 字段同步 + capability_knowledge_tool gen-pages 重生成页面；test_capability_knowledge 1637 / test_capability_completeness 1104 转绿 |
| — | test_capability_surface_parity 失败 | 预先存在：baseline 对照（master 版 capability_catalog.cpp + 同 commit）同样失败；help composition 找不到 :/terms/rs_glossary.json 资源（src/help/CMakeLists.txt:43-46 不编 help_content.qrc 入静态库），与本轨无关 |
