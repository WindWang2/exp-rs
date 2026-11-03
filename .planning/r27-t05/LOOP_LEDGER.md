# R27-T05 Loop Ledger（本轨唯一账本）

| 轮 | 改动 | 验证结果 | 通过/未通过 | 下一步 |
|---|---|---|---|---|
| 1 | 侦察：fetch/PR/issues/代码证据核对；BASELINE+TRIAGE+COLLISION 落盘 | 37 issue 详情、36 候选全分流；#1697/#1698 核实已修 | 通过 | 建 worktree |
| 2 | worktree 建立（BASE=10652fd0）；configure | **发现 P0**：tests/CMakeLists.txt 解析失败（3 未闭合括号，追加区被历史合并错切） | 未通过（configure 失败） | 重建追加区 |
| 3 | 从 8 个分支 tip 逐 target 重组追加区（31 target 零丢失） | configure exit 0；balance=0 | 通过 | P1 修复 |
| 4 | #1735 INFO<<QJsonArray 序列化；#1736 空断言→移植 e2e #784 场景；#1734 pwsh LASTEXITCODE 重置 | 代码级完成（编译验证待 lane） | 部分 | grader 修复 |
| 5 | #1744 判分：ceiling=(to-from)/(E-from)+loader 拒错侧窗；更新 r18e 钉死断言；oracle 7 用例 | test_r27_t05_grader_window：40/41 过 | 部分 | 修 #1773 用例 rubric |
| 6 | #1773 requiredStages verdict 门禁 + oracle（stage criterion 结构修正） | 81 断言 7 用例全过 | 通过 | workflow 批次 |
| 7 | #1775 严格端口解析；#1776 env allow-list+密钥形拒绝；#1771 cap+1 读；#1770 深度披露 | 代码完成；轻轨 oracle 已写 | 部分（待编译跑） | task_center |
| 8 | #1779 多父歧义拒绝（dispatch+fingerprint 两处）；#1780 派发前闸门 | 代码完成 | 部分（待编译跑） | experiment 批次 |
| 9 | #1768 全失败计数；#1781 非有限过滤+覆盖率；#1782 单元格严格化；#1787 弃权计错+accuracy_scored_only | 代码+oracle 完成 | 部分（待编译跑） | matrix/import |
| 10 | #1785 重复 pin 角色拒绝；#1786 seed 规范化+2^53 对齐；#1783 读-验-写+解析路径 digest 复核；#1784 硬链接拒绝 | 代码+oracle 完成 | 部分（待编译跑） | repair_planner |
| 11 | #1774 下溢归零；#1778 refusal-last 优先于主家族 | 代码完成 | 部分（待编译跑） | #1777 |
| 12 | #1777 全角折叠/CJK/否定感知/冲突归一 + 3 oracle 用例 | 代码完成 | 部分（待编译跑） | F 批次 |
| 13 | #1748 重 pin（原 tag 404 实证）+真实 sha256；#1749 install-tree 分级+lib 兜底；#1750 8 处 SUCCEED→SKIP；#1732 ruleset 审计（无保护实证）+提案（不实施，D2） | 下载验证 sha256 匹配；脚本 bash -n 通过 | 通过 | 大 lane 构建+跑测 |

- 并行约束遵守记录：全程 flock `/tmp/exp-rs-host-build.lock` + `-j2`；未动兄弟轨 worktree/分支。
