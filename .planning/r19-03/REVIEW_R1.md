# R19-03 REVIEW_R1（独立逆向审查，2026-10-20）

- **Reviewer**：独立 general-purpose 子代理（fresh context，非实现者；工具实测 63 次，含自行重跑三个测试二进制）
- **对象**：commit 04e81fac0（parent e12a9afbc = origin/master）
- **判定**：APPROVE with conditions——代码零缺陷；1×P1（流程）、2×P2、3×P3

## 发现与处置

| 级 | 发现 | 处置 |
|---|---|---|
| P1-1 | tests/test_help_core.cpp 不在 OWNERSHIP 可写清单（内部一致性缺口，非跨轨写；#1663/#1664 均不含该文件） | ✅ 已修：OWNERSHIP.md 补记（含依据） |
| P2-1 | ribbon（shell/ribbon_controller.cpp:856-872）Cut/Paste/Delete QToolButton 直连槽、旁路会话门控——"enabled-but-will-fail"面在 ribbon 仍在 | ✅ 已修：改用同文件 digitizing 先例的 `editingAction()+addActionButton` 投影（带原样 fallback）；Copy 保持无门控（只读层复制合法） |
| P2-2 | commit message 引用的 TEST_RESULTS.md 未随 commit 入库（当时仅在工作树） | ✅ 已修：证据文件随处置 commit 入库 |
| P3-1 | [r19d][terms] 代理对未压在 120 切点上，边界规则经由 appendDescriptors 未被钉住 | ✅ 已修：119 CJK + 星面字符（代理对恰占 119-120 码元），新增 `summary == left(119)` 精确断言 |
| P3-2 | 全新启动无 layer 事件前无 reason tooltip（与 r18-D digitizing 孪生一致，按设计） | 记录不改（一致性即契约） |
| P3-3 | r14D 新 SECTION 的 findChildren 扫描与 editingAction 结果冗余（无害双保险） | 保留（防御孪生回归） |

## Reviewer 实证复核（原文要点）

- 三测试二进制独立重跑全绿（[r19d] 6/6、m2 132/132、[menu_session_gate] 30/30、r14D 全量 370/370）
- lambda 顺序/tooltip 契约、geometry mask=0、rollback 复锁确定性（direct connection 无需事件循环）、禁用动作不响应快捷键、objectName 无冲突、spectral 对话框构造序安全、外部消费者全部先 setSpectrum、terminology 截断不破坏 related/keyword 解析、无墙钟断言、purgeProjectLayers 卫生——全部通过
- .gitignore 块模式与 r18-b 先例一致

## 复测（处置后）

- 见 TEST_RESULTS.md「R1 处置复测」节（待 build7 完成回填）
