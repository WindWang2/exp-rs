# ISSUE_TRIAGE — R23-D（对 master@ad98a7a70 现场复核）

状态分类：LIVE / FIXED_OPEN / DUPLICATE / STALE / IN_PROGRESS / OUT_OF_SCOPE / UNVERIFIABLE

## 本轨直接相关

| Issue | 标题摘要 | 分类 | 证据/基线 |
|---|---|---|---|
| #1610 | faultlab checkNumber delta/equals 等 NaN 语义 | FIXED_OPEN | #1674（r21-e）修复；fault_expectations.cpp kObservableTolerance + 注释可考；先跑 test_faultlab 系列回归确认 |
| #1611 | faultlab measureExtras index_pair 索引保持 | FIXED_OPEN | #1674；fault_observables.cpp:275-285 有修复注释 |
| #1612 | faultlab kappa score/truth 过滤不对齐 | FIXED_OPEN | #1674；cohenKappa 原始索引配对 |
| #1613 | FaultScenario::verifier rules_ref/must_fail 完整消费 | FIXED_OPEN | #1674；fault_types.cpp:256-300 严格解析 + fault_runner.cpp:168-262 fail-closed |
| #1598 | verification derivedStatus 只折叠 report.nodes | FIXED_OPEN | R21-E（#1674 帧内）；report.cpp rollUpNode/rollUpTask 无结果=Indeterminate |
| #1599 | builtin pack/contract 三值一致性 | FIXED_OPEN | R21-E；packs_builtin.cpp 未知算子→带不可判定 check 的 DerivedPack |
| #1601 | verification 与 verify 双核并存 | FIXED_OPEN（记录维护） | CMakeLists 拆分公告 + ADR 0172；权威=src/verify（C 轨），src/verification 为并行核仅测试接线；不搬库不删库 |
| #1602 | verify_adapters readFileBounded 64MB 预分配 | FIXED_OPEN | bounded_io.cpp:25 stat 定初值+cap+1 哨兵实读；复测见 TEST_RESULTS |
| #1593 | lab_pack 解析未设 stackLimit | FIXED_OPEN | lab_pack.cpp:249 stackLimit=128 + :252-263 try/catch(Json::Exception) |
| #1607 | teaching jsoncpp 解析无 stackLimit | OUT_OF_SCOPE | src/teaching/** 归 C 轨 |
| #1678 | lab::recordAnswer NaN/Inf 毒化会话 | FIXED_OPEN | #1711 帧（r22 teaching）；session_state.cpp:367,684 双端拒绝非有限值；需回归确认 |
| #1694 | 批量评分 checkpoint 无规则指纹 | FIXED_OPEN | #1711（r22 teaching）修复于 teaching_admin；本轨 D04 只读复核 |
| #1648 | src/recipes 无上限整文件读（P3） | FIXED_OPEN | a1ffd5c66（2026-10-19 "resolve full-review findings #1607 #1648"）已修：四处 stat-before-read + 16MiB 预算（kMaxRegistryBytes/kMaxSidecarBytes/kMaxRecipeBytes/kMaxLabDocumentBytes 现场读码证实）；curriculum_editor.cpp 部分（C 轨）也已 readBounded 16MiB —— 全单 FIXED_OPEN，只需回归保护 |
| #1653 | src/verification 技术债（1 项 P3） | FIXED_OPEN | 51eb7d1a6（2026-10-19 "…#1598 #1599 #1601 #1623 #1626 #1653"）已修：三文件各仅 1 次 `beginResult` 引用且 include checks_numeric.h（:21），无私有副本（rg 计数现场证实）；回归保护 |
| CT-6（非 Issue，R21-E FINAL_REPORT 遗留） | repo 学生流程 foundry 样本 256×256 UTM vs ndvi_basics 规则 128×128 WGS84 → 45/100 | **LIVE** | ndvi_basics.rules.json 自 9/13 未变（git log）；tools/sample_foundry 仍硬编码 256/UTM（sample_foundry.h:38,42）；run_lab.cmd 钉 SICNU_LAB_RULES_DIR=data\labs\grading；bundle overlay 已按 foundry 场景校准（packaging/bundle/labs/grading-overlay/ndvi_basics.rules.json）证明双权威分裂是既定设计，repo 侧缺校准副本 |

## 新发现（本轨调查）

| 编号 | 内容 | 级别 |
|---|---|---|
| D-F1 | src/lab/session_store.cpp readJsonFile：jsoncpp 1.9.6 stackLimit 超限是 throwRuntimeError（reader.h:338-342, json_reader.cpp:1040），无 try/catch → >64 层会话文件使 Json::RuntimeError 逃逸 loadFromPath，违背同文件 makeStrictReader 注释宣称的 typed rejection 与全仓 catch 纪律 | P2/P3 |
| D-F2 | src/lab/spec_runtime.cpp:320-331 expected_numeric 边界无 isfinite 门：`1e999`→inf 可作 min/max（min=inf,max=inf 通过 min<=max；-inf/inf 区间变成真空接受），与 grader_types 的 isfinite 纪律不一致 | P3 |

## 不在本轨写区的相关项 → CROSS_TRACK_FINDINGS

- #1678/teaching 边界外的 src/teaching_admin/curriculum_editor.cpp 无界读（#1648 的一部分）
- tools/sample_foundry.* 网格/CRS 参数、scripts/windows/run_lab.cmd 的 `--lab ndvi_basics`（CT-6 的 E 轨侧一行修复）
- #1491 CI 无完成结果（E）；#1690/#1691/#1692 等 agent 轨项
