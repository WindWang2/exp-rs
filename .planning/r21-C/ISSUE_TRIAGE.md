# R21-C ISSUE_TRIAGE — open issue 逐条现码分诊（2026-10-23/24 live）

格式：`(issue, 实际路径/行, 状态, 证据, 处置)`。`open ≠ 未修`——每条以 live master 源码（BASE 911e0b17a3）为准。

## A. 任务书点名种子（存在性→处置）
| issue | 位置 | 状态 | 证据 | 处置 |
|---|---|---|---|---|
| #1551 [P1] plugin.load services isObject 门 | src/plugins/host/plugin_host_worker_main.cpp:1086-1125 | **FIXED-OPEN** | live：:1096 E1002 "plugin.load services must be an object" + :1111-1119 peerString 四成员门（commit 77d70df94d，issue 正文 :1019 行号是旧基线 2d2881631f 的） | 复测（plugin 族回归）后建议关闭 |
| #1549 [P1] allowAssumed 共享缓存空操作 | src/science_context/asset_state_provider.cpp:189-256 | **FIXED-OPEN** | live：缓存存全量 resolution、allowAssumed 剥离仅在出口投影（命中/未命中同路径），注释明示设计（commit 65703e48f8） | 复测（test_science_context_*）后建议关闭 |
| #1595 [P2] cloud-max/--limit 数值门 | src/cli/cli_commands.cpp:2375-2399 | **FIXED-OPEN（主体）** | live：toDouble/toInt 带 ok 检查 → InvalidInput "--cloud-max needs a number"/"--limit needs an integer"；**残留**：:2399 `--max-items` 仍裸 toInt() | W-B 补 `--max-items` 门 + 回归 |
| #1597 [P2] parseOptions 静默跳过未知 flag | src/cli/cli_dataset_commands.cpp:86-95 + rejectUnknownArgs:225-240 | **半修** | live：dataset/experiment/reproduce 侧已 leftover 拒绝；**pipeline run/validate/resume（cli_commands.cpp:549+）、workflow run/validate/list-runs/resume（:643-830，实读确认 takeFirst 后无 leftover 检查）、commandCatalogExport、commandData 四处仍无**（grep leftover/unknown flag 在 2100-2700/643-830 范围零命中） | **W-A 本轨修复** |
| #1588 [P2] TemporalFactsProvider 只解析第一个 | src/preflight/runtime_adapter.cpp:104-183 | **FIXED-OPEN** | live：全声明集合解析；未选中可解析集合 sceneCount+invalidTimeCount 计入、未解析集合置 truncated（facts.h 契约注释同步） | 复测（test_preflight_runtime_adapter）后建议关闭 |
| #1594 [P2] substitutePlaceholders 二次注入 | src/workflow/placeholder_grammar.cpp | FIXED-OPEN（R19-02 复核 :232-252 单遍；R20-03 维持） | 维持判定；本轮补一次抽查 | 抽查 |
| #1572 [P2] onReplyFinished 流式回调重入 | src/agent/ | FIXED-OPEN（R19-02 修 #1570-1574 批；R20-03 抽查 #1573/#1574 一致） | 待本轮抽查 #1572 现场 | 抽查 |
| #1589 [P2] science_context 失效传播接线 | data_platform_tools.cpp notifyProjectSwitch | FIXED-OPEN（R19-02+#1666，R20-03 sweep 复核 agent_adapter.cpp:211-214） | 维持 | — |
| #1598/#1599 [P2] verification | src/verification | OUT-OF-SCOPE | E 轨域（R20-05 核查 fixed-open） | 不碰 |

## B. R20-03 移交的工作包（本轮候选）
| ID | 内容 | 状态 |
|---|---|---|
| W8-red | plugin 诊断码族 5 二进制 master 同构红：test_plugin_host_process(:1259 sawTypedRefusal)、test_plugin_host_boundary_r4(1)、test_plugin_lifecycle_halffail_r4(6)、test_plugin_lifecycle_unload_order_r4(1)、test_exprs_plugin_loader(16)——E5005 访问模型拒绝未触发、symlink TOCTOU 逃逸未观测、LibraryLoadFailed/TrustRejected/ManifestInvalidJson 诊断缺失 | 待构建后重跑确认 → 修复（src/plugins+src/sdk 写区） |
| D5 | plugin_host_session.cpp:630,701 kill 选举败者无锁读 mProcessHandle（#1644 同款） | 待修（防御性） |
| D6 | 无界文件读：workflow_run_lock.cpp:51,167；preflight/capability_mirror.cpp:293；sdk plugin_manifest/registry/discovery/snapshot | 待修（最高频路径加界） |
| D9/CT-B | CLI `--approve` 静默 no-op（全库零消费者）+ 无 `--repair-approval` 通道（cli_agent_ops_commands.cpp:145-153） | 待修（CLI 参数契约灰区内，W-D9） |
| CT-6 | 7 cartography 工具 schema 缺项 → test_surface_snapshot registry 腿红 | 跨轨（D 轨），保持 KNOWN_BASELINE_FAIL 证据新鲜 |

## C. 维持 DEBT（前轮决策）
- #1628-3 temporal phenology 三工具未接线（设计如此；接线=新功能面 → M4 候选项，需评估）。
- #1629-1 run/resume 启动重复（重构风险>收益）、#1630-2 EnumTable 四份拷贝（纯重构）。
- D8 ClearGuard 无行为差异（R20-03 论证无法造红）。

## D. P2/P1 批量 issue 归属对账（标题级，C 轨写区内才行动）
- #1570-1574（agent/agent_ops P2 批）：R19-02 修（fixed-open）。
- #1596（6 份 flag 解析器）：CLI 契约域，重构债，候选 M4（与 W-A 合并考虑）。
- #1603-1605（src/runtime）：B 轨。#1608-1609（src/python）：跨轨。#1606（contracts output 参数）、#1607（teaching）：跨轨。
- #1643/1644/1645/1649/1650/1654（swarm P3 批）：R19-02 修（fixed-open）。
- #1491 CI 失效：环境事实，本地全测替代。
