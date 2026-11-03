# R27-T05 Review R1（对抗性，2026-11-03）

**隔离声明**：原计划独立子代理执行；子代理因宿主账号 5 小时配额上限失败（重置 19:45Z+8）。降级为主代理内联对抗评审：与实现隔离开销不可claim，但方法论独立（清单驱动逐 hunk 重审 + 机器审计），全程留痕。R2 同样内联但换视角（issue 原文↔代码逐条证明 + 兼容性/负例）。

## 覆盖面（查了什么）

全部 17 个生产文件 hunk、3 个新 oracle、9 个被改既有测试、CI/scripts/packaging、tests/CMakeLists.txt 重建完整性（**机器审计：BASE 953 target vs HEAD 956 = 零丢失、仅新增 3 个本轨 target**）。

## 发现与处置

| ID | 级别 | 位置 | 问题 | 处置 |
|---|---|---|---|---|
| F1 | **P1** | evaluation.cpp #1782 | `qint64( raw )` 对 finite 越界字面量（1e30）是 UB（旧 toInteger 会钳制拒绝；新代码漏了范围检查） | ✅ 已修：kMaxCount 范围门 + 错误消息升级；受影响 4 套件重跑全绿 |
| F2 | **P2** | task_center.cpp #1780 门 | Failed 路径未清 `m_readySerial`：aging sweep 每 50ms 空扫终态 id 直至注销（无复活可能——sweep 与准入均经 isLaunchCandidateLocked 过滤，已核实） | ✅ 已修：pop 时同步 remove |
| F3 | P3 | grader_matcher.cpp #1777 | 否定检测仅前缀窗口（16 字节）；"X or not Y" 后缀否定与 "never ever X"（token 不在窗口尾）不捕获 | 记录为文档化限制（注释已述"in front"）；教学场景前缀否定覆盖主要误判 |
| F4 | P3 | #1780 门 | 参数含字面 "$step.x" 文本的合法用例将被拒绝（此前同样以字面量下发） | 语义即 issue 要求（pre-launch refusal）；oracle 已含 "cost 5$" 反例不触发 |

## 明确核查过且干净的项

- #1744 AtLeast/AtMost ceiling 镜像数学（含 tolerance 重锚定、C++ 直构路径 toBand>0 守卫）
- #1777 UTF-8 解码：截断/续字节/超长编码均安全（逐字节透传，无崩溃路径）；冲突检查归一化
- #1776：仓库内 getenv 仅 placeholder_grammar 一处且已闸控；data/pipelines 全部 env 名在 allow-list
- #1775：resolvePlaceholderPort 仅 2 调用方（dispatch+resume），语义一致
- #1787 零分母；#1786 "0" 合法；#1778 严格弱序；#1774 条件下溢归零；#1770 空 frontier 边界
- #1783 relocation 使用一次性 digest 表（无每条目重读）；#1784 nlink 查询失败即拒绝
- ep8 bucketed-queue BASE-red 对照已复核（BASE 三文件回退重建同样失败）
- SKIP() 在 Catch2 3.7.1 可用（test_paths 编译+运行证据）
- ci.yml 其余 pwsh 步骤尾态安全（末条原生命令均成功路径 0）

**R1 结论：4 findings（P1:1 P2:1 P3:2），P1/P2 已修复并重测，P3 记录为限制。**
