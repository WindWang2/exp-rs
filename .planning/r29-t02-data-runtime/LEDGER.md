# LEDGER — R29-T02

格式：第N轮 | 改动 | 验证 | 结果 | 下一步

| 轮 | 日期 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| 1 | 11-08 | 基线：worktree/分支/origin 读取（BASE=130990a396），0 open PR；D-T05-CHAIN1 根因、VRT provider 缺陷、H1 线程事实全部源码实读 | BASELINE.md | ✅ | configure |
| 2 | 11-08 | configure dev-default + OpenCV_DIR | EXIT=0 (log-configure.txt) | ✅ | 全量构建 |
| 3 | 11-08 | 全量构建 -j4 双锁 | ICE×4 + ld segfault → make err 2（1444 obj；工具已产出 sicnu_generate_samples） | ❌ 瞬态（内核日志仅 1 条 ld segfault；内存充足）→ 断点续编 | resume build |
| 4 | 11-08 | WP02 代码：resumable_tile_run R8 回调+降级；chunked_run 默认验证；spectral 身份戳+输入指纹+verify 回调 | 编译中 | ⏳ | 续编+测试 |
| 5 | 11-08 | WP05 preflight targetDiffers 分支；WP06 provider warp（pinned grid、对齐 extent、warpRaster staged 中间文件、structureFromDataset 提取） | 编译中 | ⏳ | 续编+测试 |
| 6 | 11-08 | WP09 代码：RSOperator::requiresHostQtThread；cartography 三 operator 标记+requireQgisHost 线程检查；JobEngine host lane（queued+semaphore+cancel grace）；TaskCenter waitForTask/Pipeline 事件切片 | 编译中 | ⏳ | 续编+测试 |
| 7 | 11-08 | 测试：test_r29_t02_published_claim（6 假设+R8）、test_r29_t02_virtual_warp（preflight+warp oracle）、test_r29_t02_chains（链②③）；CMake 注册 ×3；WP01 六假设脚本 | 待构建后运行 | ⏳ | 运行 |

## RED 证据策略

D-T05-CHAIN1 的 RED 由 r28-t05 已记录（chain1_teaching.sh 在本 base 上复现 false-green，ISSUE_TRIAGE 完整调用链）；本轨 WP01 脚本在同一 HEAD 运行修复后代码为 GREEN，并对 H-C/H-D 的自愈/漂移行为给出断言。R8 框架层测试断言 demote 后 R2 复用（tilesReused=4, computes 不变）——该断言在未打补丁代码上不可能通过（旧代码 marker 命中直接返回）。

## 构建/环境事实

- dev-default Debug + tests ON；OpenCV=/home/kevin/.cache/exp-rs-agent/opencv5-prefix。
- ICE 复发预案：同类 ICE 再现 → -j2 重试（资源策略允许）；仍失败 → 单文件复现+报告。

| 8 | 11-11 | R1 修复（P0×1+P1×3+P2×4+P3×4 修，P3×2 记录）+ 重编 + 全部验证 | 见下 | ✅ | R2 复核 |
| 9 | 11-11 | 新测试：published_claim 73 断言✅；virtual_warp 144✅；chains 100✅（fixture 修正 3 处：Result 歧义/GDAL3.13 void Transform·SetAxisMapping/journal 终态与 envelope-less mapspec） | 直跑二进制 | ✅ | 回归 |
| 10 | 11-11 | WP01 CLI 六假设 16/16✅（H-C 自愈/H-D 内容漂移/H-E 跨工作区）；cp11 SICNU_CP11_THREAD_PARITY=1 交付契约 24 断言✅（三表面字节一致+真实 ink） | 脚本+ctest | ✅ | 回归 |
| 11 | 11-11 | 回归批量 68 匹配：55 实跑全过 + 13 NOT_BUILT（早期构建中断残留）补编后 35/35 全过 → 68/68 全绿 | ctest-run.log + ctest-run2.log | ✅ | commit+R2 |
