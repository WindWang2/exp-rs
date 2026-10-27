# r23-A LOOP_LEDGER(逐轮账本)

| 轮 | Oracle 差距 | 聚焦改动 | 验证 | 结果 | 判定 | 下一步 |
|---|---|---|---|---|---|---|
| R0 基线 | 全部 | fetch+gh 核验(0 open PR/144 issues/default=master);worktree 建立;13 项科学相关 issue 逐条源码核验(#1687 LIVE,12 项 FIXED_OPEN,其余 OUT_OF_SCOPE);3 审计代理派出(时序/高光谱/地形+辐射);SAR polsar 人工核验 CLEAN | git/gh/grep | 分诊完成 | PASS | 审计回收→选片 |
| R1 审计 | 选片证据 | 3 后台审计代理(时序 4.1M tok/高光谱 1.9M/地形 5.4M)+ 主会话 SAR/调用链核验:9 项 LIVE(1×P1 viewshed 亚米、2×P2 曲率/ROI 单位、6×P3)+ #1687;全部本人复核源码行号与反例 | 代理报告+人工复核 | 发现清单落 TASKS T1-T11 | PASS | 写 RED 测试 |
| R2 RED | 真实 RED 证据 | 10 个既有测试文件追加新 SECTION(math_utils/viewshed/foundation5/spectral_roi/solar/temporal_fit/mnf/unmixing_fcls/spectral_table/image_enhancement),master 源码构建后批量跑 → 预期各恰好新 SECTION 失败 | 待跑 | PENDING | — |
| R3 RED 批次 | 10/10 新用例失败 | master 源码+新测试构建(GREEN_BUILD 前一构建,exit=0)后逐二进制运行:恰好每个新 TEST_CASE 失败、既有用例全绿;temporal_fit 为 SIGABRT(stack-protector 捕获越界写);terrain_solar 实测为噪声方位(171/8.8)而非审计预测的 NaN——缺陷同真,修复方案随之调整(阈值极区分支) | 见 TEST_RESULTS.md | 10×RED 确认 | PASS | 应用修复 |
| R4 FIX | 11 修复脚本应用 | 全部断言成功应用;viewshed 初版量化在 0.3 情形仍 12 暗元(浮点整数边界碰撞,独立复现程序确认)→ 量子减半加固,10 组对抗格网 0 暗元 | dbg 程序 | 修复完成 | PASS | GREEN |
| R5 GREEN | 两轮全绿 | GREEN1+GREEN2:10 二进制 exit=0,断言数两轮逐一相同(10,972 断言);12 个主题提交(1 planning+10 fix+1 docs) | 见 TEST_RESULTS.md | 全绿 | PASS | 邻域回归 |
| R5.1 教训 | 浮点边界 | 环量化类算法的"整数倍格距"输入必须进对抗测试集;审计报告的失败模式预测(NaN)与实测(噪声有限值)可能不同,修复前必须独立复现 | — | 登记入案 | PASS | — |
