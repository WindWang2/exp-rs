# RISK REGISTER
| 风险 | 等级 | 说明/缓解 |
|---|---|---|
| provenance exists-then-write 竞态 | 低 | evidence.cpp 二次检查与 atomicWrite 之间仍有窗口（rename 无 NOREPLACE）。触发需引擎 writer（step 完成时一次）与 harness persist 同瞬；最坏后果：引擎 sidecar 被 harness 覆盖（引擎 lineage 丢失，但 artifact+verification 完好）。R1 取舍：不引入 renameat2/ReplaceFile 语义变更（跨平台风险 > 收益）。 |
| planner 门当前只被测试触达 | 中（登记） | planScientificWork 在 src/ 无生产调用方（仅 tests）；science_context 投影不填 ctx.assets。两个新门（CRS/band-roles）随接线自动生效；接线时须同步投影 bandRoles（bundle.h 已有数据）。本轨不新建 planner 接线（防重复开发）。 |
| band-role 门"任一资产任一 role 即过" | 低 | contracts 不携带 role 需求清单（仅 wavelengthPolicy=band_roles），planner 无法更严；角色是否满足由 science preflight（workflow_analysis band_roles 检查）兜底。注释已声明归属。 |
| test_capability_surface_parity 失败 | 预先存在 | glossary 资源未编入测试 target（src/help/CMakeLists.txt:43-46）；master baseline 同失败。建议后续轨修（本轨范围外）。 |
| 操作事故：误 pop 共享 stash | 已发生/已记录 | 见 FINAL_REPORT「过程事故」。 |
| 未测矩阵 | 已知 | 无 GPU/远程/Windows/macOS 执行；MCP 真实 --mcp E2E 未跑（#1727 独占 mcp_server/pi，本轨禁触）；Pi 侧回归属 #1727 范围。 |
