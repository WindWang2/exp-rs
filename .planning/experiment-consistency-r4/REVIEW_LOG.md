# R4 REVIEW_LOG — 独立对抗评审记录

## 评审安排（Phase 5）

- 评审员：独立只读 subagent（与实现分离，2.69M tokens，58 次工具调用）。
- 评审范围：`origin/master...HEAD` 全量 diff（当时 15 提交）+ 工作树在途改动。
- 总判定：**BLOCK**（后经修复批次转为可达 SHIP-WITH-FIXES；P0 清零）。

## 评审发现与逐条处置

| 编号 | 级别 | 证据 | 处置 | 复验 |
|---|---|---|---|---|
| P0-1 | P0 | 已提交 CMakeLists 注册了未提交的 test_experiment_bridge_r4.cpp → 干净 checkout 配置失败 | 随 5925e8aed9 提交 bridge 测试 + 分区修复 + CMake 行 | git ls-tree HEAD 确认在树；定向构建编译该目标 |
| P0-2 | P0 | lab_report_writers.cpp:31 jsonCodeFence 调用未声明的 compactJson（编译失败） | compactJson 前置声明 | 定向构建编译 bridge 目标（待重链确认） |
| P1-1 | P1 | study_analysis.cpp:152 analyzeStudy 仍用 runById——损坏行静默计"无证据"，扭曲曲线/包络/Pareto | Pass-1 改 runRecordById；corrupt → 整体 typed 拒绝；absent 保持无证据语义 | 轻量道 1374 断言/10 例绿 |
| P2-1 | P2 | runsForCell limit+1 探测被 outgoingEdges 的 10000 钳制击败（limit≥10000 时截断不可检测） | limit ≥ 10000 直接 `experiment.matrix_invalid` 拒绝（保证探测有效域） | 1001 边端到端用例仍绿；边界语义文档化 |
| P2-2 | P2 | promotionRecordById 零消费方死 API | 补 corrupt vs not_found 双腿 oracle（直接 sqlite 注入 torn 行） | consistency_r4 271 断言/10 例绿 |
| P2-3 | P2 | STOPPED 报告的 stoppedReason 被 projectRunMatrix 抹平（issues 从未填充） | 一行修复：issues.append(stoppedReason)；补 overflow→stopped→issues parity 用例（1138 断言/5 例绿） | parity_r4 绿 |
| P2-4 | P2 | repeat 分类器 byRef 路径仍 fail-open——全损坏时落 "New" | byRef 迁移 runRecordById；全损坏 → `experiment.repeat_unreadable_twin` typed 拒绝（与 twins 路径对称） | 轻量道绿（ci 编译+测试） |
| P3-1 | P3 | lineageEdgePage COUNT 失败时与空表不可区分 | count 失败 → truncated=true（保守） | 编译绿 |
| P3-2 | P3 | EquivalentRerun determinism note 读取仍 runById | 接受：matchedRunIds 仅含可读 run（:190-197 守卫），同 run 二次读取一致；不扩面 | 书面记录 |
| P3-3 | P3 | 桥接 steps 路径 runById corrupt→evidence_absent 与 kCodeRunCorrupt 不一致 | 接受：展示接缝（D3 展示语义），warning 链可见 | 书面记录 |
| P3-4 | P3 | 缺 seed 键的 legacy bundle 从"静默 seed=0"变拒绝（超出⑧声明域的额外收紧）；kMaxLosslessSeed 第三份 2^53 拷贝 | 接受：与同文件 fingerprint 缺失拒绝先例一致，方向为收紧；BACKLOG 已记载 | 书面记录 |
| P3-5 | P3 | BACKLOG ⑦ 状态过期（FIXED 落地但标 PENDING）、提交粒度失实 | 已修正状态与归属提交说明 | 本次提交 |
| P3-6 | P3 | ⑤ oracle 真值用被测函数自身计数（轻度自证） | 改直查 raw sqlite COUNT | consistency_r4 绿 |
| P3-7 | P3 | graph 截断 true 分支不可测（固定 100000 页上限） | 接受：page 级截断已测；graph 级注记，构造函数加参属 API 扩面 | 书面记录 |
| P3-8 | P3 | ledgerForMatrix Result 化后零调用方 | 接受：为 ④ 合同完备性保留（公共 API 与 runsForCell 对称） | 书面记录 |

## 抽查记录（评审员实测）

- 白名单：26 diff 文件逐一比对，**零越界**。
- 声称修复核验：⑤④⑧②⑨⑪ 六条逐一"实现坏了测试会不会红"判定为真修（详见评审原文：SQL 先过滤后限页绑定序、1001 边落盘对账、checksum 重铸隔离 seed 决策、OA=0.75 手算真值、total=7/limit=5 三态、raw sqlite 注入损坏行）。
- 传播完整性：runsForCell 四调用方全处理 failure，无遗漏。
- 零漂移：incomingEdges 逐字节等价、buildStudyReport 非 overflow 路径不变、CLI 路径未触碰、allLineageEdges 兼容包装保序。
- 构建实测：轻量道六套件全绿；故障注入点经 grep 确认真实存在。

## 复评

修复批次 5925e8aed9 + 规划文档提交后：P0=0、P1=0、P2 全部处置（2 修 1 补测 1 修+测）、P3 处置（1 修 1 修 4 书面接受）。终验以重链载体 ctest 双跑为准（EVIDENCE.md §5）。
