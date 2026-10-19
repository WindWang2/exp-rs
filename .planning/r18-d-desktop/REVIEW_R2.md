# R18-D Review R2（第二轮独立 reviewer，全新 context，未参与修改与 R1）

- Reviewer context: 全新 general-purpose 子代理，只读，审全部 7 commits（含 R1 修复 de0db471d）
- 结论：**APPROVE_WITH_P2P3**（0×P0，0×P1，0×P2，5×P3）

## R1 修复复审（全部成立）

1. addAction(QIcon, QString, QKeySequence, Args&&...) variadic 重载在 Qt 6.12 存在且与旧形式语义等价（qwidget.h:609 实证）。
2. menus.cpp 19 个 addGatedDigitizeAct 调用与测试 menuGated 19:19 精确一致，无多漏。
3. 12 个重复 objectName 的双列表 gating 幂等，reason 属性各写各的；ribbon 布线（toolbar 优先）不受影响。
4. CapturePoint 在 polygon 层 enabled 符合 QgsMapToolAddFeature 语义；trim/chamfer polygon 能力有本仓工具源码佐证。
5. atlas 字段级反例成立：fixture 字段仅 name/rank，no_such_field 缺失；setFilenameExpression→prepare 拒绝。

## 其余 commit 抽查（全部通过）

- W4 拒启路径无 modal（offscreen 下 76/76×2 实证）；快照重解析完备。
- C3 itemSheet 与规则侧 itemPageSize 逐条一致；CHART_OVER_MAP 的 page-0-only skip 自洽。
- C1 pdf/svg 页选择被 validate 完整拒绝；多页循环与 SingleNamePageExporter 无同名互写。
- C6 atlas-disabled+坏表达式路径安全（typed refusal + deleteLayout）。
- W6/W1a 高频 taskUpdated 无热点（150ms debounce + singleShot 合并）。

## P3 与处置

| 发现 | 处置 |
|---|---|
| P3-1 main_window.h m_menuDigitizingActions 注释只列 7 项（实 19） | **已修**（后续 commit） |
| P3-2 12 个同名项断言的是工具栏孪生，菜单侧回归不可见 | **已修**：menuDigitizingActions() 访问器 + 测试直接钉菜单孪生（[r18d] 188/188×2） |
| P3-3 TEST_RESULTS 未随 R1 更新数字 | **已修**：本文件与 TEST_RESULTS 补齐最终数字 |
| P3-4 atlas 禁用时仍强检 filename_expression | **已修**：注释声明故意（strict-while-checkable） |
| P3-5 Edit 菜单无孪生命令（Delete/Cut/Paste）未 gate | **已记录** CROSS_TRACK_FINDINGS #5（既有非回归，后续 WP） |

## 程序性检查

- git diff --check 干净；所有权全部在 OWNERSHIP 独占区。
- 证伪 14 条路径全部列出，均不成立。

满足 Oracle-F：R1 的 P1 与 P2 已修并复验；R2 无 P0/P1/P2，P3 全部处置（修或记录原因）。
