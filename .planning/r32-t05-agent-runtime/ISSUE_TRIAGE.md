# ISSUE_TRIAGE — R32-T05（基线 53412c86 复核，2026-11-17）

| Issue | 状态 | 层级 | 本轨处置 |
|---|---|---|---|
| #2073 无context beginRun失败误报+pending泄漏 | CONFIRMED_OPEN（静态，HEAD行号核对） | 静态 | WP02 修复 |
| #2074 dock/服务裸指针+setContext静默 | CONFIRMED_OPEN（静态） | 鞾态 | WP03 修复 |
| #2075 流式期项目切换→checkpoint挂起 | CONFIRMED_OPEN（静态） | 静态 | WP04 修复 |
| #2076 cancel用closeTimeoutMs掐死tool清理 | CONFIRMED_OPEN（静态） | 静态 | WP05 修复 |
| #2077 Starting窗口close与迟到ready竞态 | CONFIRMED_OPEN（静态） | 静态 | WP06 修复 |
| #2078 无效direct token被armed兜底 | CONFIRMED_OPEN（静态） | 静态 | WP07 修复 |
| #2079 DecisionRecord内层类型校验缺失 | CONFIRMED_OPEN（静态） | 静态 | WP08 修复 |
| #2080 短apiKey子串碰撞→checkpoint必失败 | CONFIRMED_OPEN（静态+scratch probe） | 协议 | WP09 修复 |
| #2081 freshness门禁5 FAIL + bilingual断言取英文 | CONFIRMED_OPEN（报告者动态复现；本轨复核测试fixture部分） | 混合 | WP14：测试fixture归本轨；.ts/lupdate归T06（HANDOFF） |
| #2106 CI调用不存在build/test_python_agent_mvp + 缺SLOW标签 | CONFIRMED_OPEN（静态+报告者动态） | 静态+动态 | WP14 修复（不触发CI） |
| #2107 r23_e dtype测试期望-32603实际-32000 | CONFIRMED_OPEN（报告者动态复现） | 动态 | WP14 修复（本机AF_UNIX受限，见LEDGER） |
| #2109 exprs_plugin_host_worker无install规则+stage静默跳过 | CONFIRMED_OPEN（静态，cmake_install.cmake实测空） | 静态 | WP15 修复 |
| #2111 SDK raw fs::path(std::string) ACP解码 | CONFIRMED_OPEN（静态；Windows运行时UNVERIFIED） | 静态 | WP16 修复（用portable UTF-8 helpers） |
| #2112 verifyChecksums大小写敏感 | CONFIRMED_OPEN（静态） | 静态 | WP17 修复 |

注：
- 其余 #2073–#2129 中不属本轨所有权的问题不在本表展开。
- #2081 的 `resources/translations/sicnu_zh_CN.ts` 与 lupdate 门禁归 T06（R32 规则）；本轨只改 `tests/test_bilingual_delivery_ui.cpp`（整文件归本轨）并写 HANDOFF。
