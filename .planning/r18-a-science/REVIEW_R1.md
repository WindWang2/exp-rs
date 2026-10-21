# R18-A REVIEW_R1 — 独立评审记录

评审者：全新 Explore 子代理（与三路审计、全部实现/提交均无交集），只读，对
`git diff origin/master...HEAD` 逆向审核 + 5 项定向证伪。

## R1 独立评审（2026-10-19，评审者：未参与实现的全新只读 context）

结论：**修复前不可合并**——P0×1、P1×1、P2×3、P3×5；5 项定向证伪中 F4 成功证伪（quac 提交
留下悬空 validEnd，编译必败）。其余证伪尝试失败（修复成立）：F1 融合 Inf 洗涤无旁路、
F2 band 校验无多波段破坏、F3 SoA 逐位等价验证通过、F5 #1514 恢复逐字节一致。

P0 根因复盘：构建与编辑竞态——make 依赖 mtime，编辑晚于 .o 时跳过重编译，EXIT=0 为假象
（此前 atmospheric「全过」记录对 HEAD 源码状态无效，已由本次修复后的真实构建+双跑覆盖）。
工程教训入 LOOP_LEDGER：每次 GREEN 后验证 .o mtime > 源码 mtime。

修复（commit 529481e72）：P0 nth_element 改 scratch.end()；P1 rs:mosaic 算子路线 isfinite；
P2-1 quac 输出谓词；P2-2 流式 linear panWeight 钳制；P2-3 dos1Histogram 谓词；P3-2 死分支
移除；P3-3 基准补第 3 次（512²: 165.4MiB/1062ms；1024²: 347.2MiB/3729ms，与前两次一致）。
验证：11 套件 ×2 全绿（atmospheric 45 用例/37507 断言，fusion 44/1687，corpus 9/288 等）。

P3 未处理项记录：P3-1 resolveBandParam 双打开（TOCTOU 退化为旧 GdalError，无安全影响）、
P3-4 pixelToNode=0 瞬态标记注释化（已在代码注释）、P3-5 CF-5 已知限制维持。
