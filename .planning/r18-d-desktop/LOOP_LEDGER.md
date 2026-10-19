# R18-D LOOP LEDGER

格式：`轮次 | 假设/改动 | 文件 | 验证命令 | 退出码 | 结果 | 下一步`

## 第0轮（Recon）
- recon | LIVE 基线 a60c8831c；#1513 已合并；唯一开放 PR #1534 零交集；两份只读审计产出 16 候选 | .planning/* | preflight/overlap_scan/stale_branches/narrow_targets/gh pr list | 0 | 通过 | 建 worktree + 规划文件（已完成）
- build | configure+12 测试目标构建排入全局锁队列（后台） | build-r18-d | /tmp/r18d-configure.log, /tmp/r18d-build.log | - | 进行中 | 等锁；期间开始 WP-W4 代码亲读

## 第1轮（RED 准备，src 未动）
- RED×11 | 全部写入测试文件，生产代码零改动 | tests/{test_r14_D_vector_edit_honesty,test_mapspec,test_cartography_quality,test_cartography_production_11,test_layout_tools}.cpp | 待构建后运行 | - | 待观察 | 等全局锁；RED 清单: W4 APP-10 / W2 menu-gate / W3 activeEditable / W6 inflight / W1a external-edit / C2 typed-text / C5 legend-title / C3 per-sheet-repair / C1 single-page-export / C6 atlas-expr / C7 nan-dpi

## 第2轮（RED 实证 → 修复 → GREEN）
- RED 实证 | 11/11（C2 mapspec:1001/1027/1059; C3 quality:291/293/300 ledger=still_reported; W4 r14D:382/383/431/432; W2 :1011/:1037 nullptr; W3 :1079/:1083; W6 :1114; W1a :1147/1152/1155; C1 prod:967 result.ok=false; C6 prod:1006/1007 layout非空; C7 首跑 PASSED→断言加强为 error 须含 DPI） | 只改测试 | 直接运行四个测试二进制 | 见 TEST_RESULTS | 全 RED |
- FIX×11 | main_window_vector.cpp(W4+W2 gate+mask)、main_window_menus.cpp(W2 注册7动作)、main_window.h(W2 成员)、selection_context.cpp/.h(W3+scheduleRefresh public)、main_window_connections.cpp(W6+W1a)、mapspec.cpp(C2+C5)、quality.cpp(C3 itemSheet×6 handler)、export.cpp(C1)、mapspec_compiler.cpp(C6)、layout_service.cpp(C7) | src 修复 | build17 EXIT=0 | 0 | 通过 | 回归发现 C6 过严
- 回归修复 | C6 加 coverage-layer 前提（无 coverage 时保持原宽容；export 路径自会 typed 拒绝）→ test_mapspec 全量 4 失败清零 | mapspec_compiler.cpp | build18 + 全量重跑 | 0 | 2830/2830 GREEN | 完成
- GREEN×2 | r14D[r18d] 76/76 ×2; mapspec[r18d] 23/23 ×2; prod11[r18d] 16/16 ×2; layout robustness 24/24 ×2 | - | 直接运行 | 0 | 全 GREEN | 回归批次见 TEST_RESULTS

## 第3轮（Review + 交付）
- R1 | 独立 reviewer（新 context）审 6 commits | REQUEST_CHANGES: P1-1 trim/extend mask + P2-1/P2-2 | de0db471d 全修 | 修复后 [r18d] 149/149×2 + 全量绿 | 通过
- R2 | 再新 context 审 7 commits | APPROVE_WITH_P2P3（零阻断） | c889bbb4f 处置 P3（菜单孪生测试面/注释/故意严格声明/CROSS_TRACK#5） | [r18d] 188/188×2 + 340/340 | 通过
- 交付 | 10 commits push 成功（代理 3 次重试）；PR #1661 创建成功 | https://github.com/WindWang2/exp-rs/pull/1661 | 0 | 完成 | 不等线上 CI；未达项见 TEST_RESULTS NOT_EXECUTED
