# DECISIONS — R25-C

1. **漂移报告而非重注册**：Pi ExtensionAPI 无工具注销能力，崩溃 respawn 后工具目录漂移以「检测+报告+console.warn+exprs_status 披露」处理，不盲试二次 registerTool（语义不可验证）。
2. **bridge 级错误类型码独立于工具级**：server 产生的 JSON-RPC error 保留原 code/data 不重打类型；仅 bridge 自建错误附 bridgeError/retryable。
3. **run_id 诊断为只读观察**：runForRunIdReadOnly 不取 run lock、不改状态；resumable 是 ADVISORY（镜像 resumeRun 状态墙），所有权裁决权留在 resumeRun。active 状态的 checkpoint 报「不可直接恢复+resume 会 reconciles」而非谎报 resumable=true。
4. **prerequisites/limitations 键缺省省略**：无声明不入键（有界 token 成本），不输出空数组。
5. **r18c timeout 测试竞态修复**：跨进程 append 与测试读取之间的预存竞态（基线 2/8 复现）以 bounded poll 修复；id_null flake 机制未定（单跑 0/25），记录不盲改。
6. **W6 不强造变更**：插件 E6xxx 类型化面已被 r23-E 覆盖，本轨以邻域回归维持 + 文档结论交付，不为了改而改。
