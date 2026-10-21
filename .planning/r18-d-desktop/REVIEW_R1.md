# R18-D Review R1（独立 reviewer，未参与修改）

- Reviewer context: 全新 general-purpose 子代理，只读 worktree，审 `git diff origin/master...HEAD`（当时 6 commits）
- 结论：**REQUEST_CHANGES**（1×P1，2×P2，5×P3）
- 证伪路径 10 条，9 条不成立（toggleEditing 悬空/菜单闪变/selection 回退回归/connect 泄漏/单页分支可达性/C6 兜底/C7 依赖方/itemSheet 一致性/测试 vacuous/所有权），1 条成立为 P1。

## 发现与处置

| 级别 | 发现 | 处置 |
|---|---|---|
| P1-1 | Trim/Extend 掩码 Line-only 与工具真实能力不符（本仓 qgsmaptooltrimextendfeature.cpp:33-39 FeatureFilter 接受任意 edge；上游 .ui 无限制；QGIS 文档支持 line+polygon）——polygon 会话从 enabled-but-fail 反向修成 disabled-but-would-succeed，且测试锁死错误契约 | **已修** de0db471d：掩码改 Line|Polygon，测试改为锁正确契约（trim 在 polygon enabled + reverse 仍诚实禁用） |
| P2-1 | Digitize 菜单中 9 项带门控工具栏孪生的命令仍恒 enabled（Rotate/Offset/Reverse/Reshape/SplitFeatures/Simplify/AddRing/FillRing/DeletePart） | **已修** de0db471d：全部并入 addGatedDigitizeAct 门控（连同 CapturePoint/VertexTool/Move 共 19 项），测试 menuGated 覆盖 19 项 |
| P2-2 | C6 只被语法级反例钉住（"1 + * 2"），字段级（'x' || no_such_field）无测试 | **已修** de0db471d：补字段级反例断言 |
| P3-1 | 报错文案 "not a number" 以偏概全（bool/null/object 同触发） | 保留：文案与 colorbar 先例一致，非阻断 |
| P3-2 | qgis_type annotation 的验证/编译不对称 | 保留：更严不致错（提取路径不产 text） |
| P3-3 | itemSheet 在 sheet<1mm 时 clamp UB | 保留：pages 校验 >0 但实际不可达（修复与规则侧同暴露面） |
| P3-4 | CROSS_TRACK C7 措辞不准（(0,1) 行为变化未述） | **已修**：措辞更正 |
| P3-5 | MAP_OVERLAP 槽搜索跨页障碍过度保守 | 保留：既有行为，未变差 |
