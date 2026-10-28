# R25-B CROSS_TRACK_FINDINGS

（暂空——发现跨轨问题在此登记，附 file:line 与复现。）

## 交接（C/D 轨可消费的契约）
- F1 ImportPreflightReport JSON（sicnu.import.preflight/1）：供 D 轨 data_manager_panel 导入向导消费（本轨不改 UI）。
- F2 InferencePlan JSON：CLI/MCP 可直接打印；C 轨 agent 工具如需暴露 dry-run 可直接调用该 seam。
- F3 deviation_details[]（domain/remediation）：capsule/复现 UI 可直接渲染。
