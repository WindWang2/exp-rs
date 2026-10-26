# R22-05 ORACLES

1. **W1 oracle**：任务运行中析构 runner → `QApplication::overrideCursor()` 恢复为空（不残留 WaitCursor）；父 widget 先亡 → 完成回调不接触悬垂指针（QPointer 判空）。RED：master 代码上构造 → 析构（不等任务完成）→ overrideCursor() 仍为 WaitCursor。GREEN×2。
2. **审计工作包 oracle**：每个缺陷先有失败复例（真实运行的断言红/UB 证据），修复后 GREEN×2；不扩大容差、不删断言。
3. **全局交付 oracle**：本轨一个 PR；关键测试双跑；R1/R2 落盘；PR URL 真实返回。
