# LOOP_LEDGER — r21-A

| 轮 | 里程碑 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| 1 | M0 | LIVE 盘点、worktree 建立、5 份基线文档 | git/gh 输出存档 | 通过 | 基线构建 + 缺陷勘查 |
| 2 | M0 | worktree+基线文档；基线构建踩坑：vendored GDAL 带 flang wrapper2_* 未定义符号（stub 方案）、SicnuSharedLinkGuard+新 binutils 需 shared 也加 --allow-shlib-undefined、vendor prefix 静态 libexpat/sqlite3/zlib/zstd 需 pin 系统 .so | BUILD_EXIT=2→定位→修复配置 | 通过（构建重启） | 缺陷修复批次 M2 |
| 3 | M2 | 16 项缺陷修复完成（15 采纳+1 驳回），9 个原子提交；构建修好 join 编译错后续建 | 待测试验证 | 待验证 | GREEN×2 波次 |
| 4 | M6 | R1 fresh 评审：18/19 干净；P2 move-assign 陈旧缓存 + 6 P3 → 全部修复+回归 | 受影响族两遍绿 | 通过 | R2 |
| 5 | M6 | R2 fresh 评审（含 27 变异审计）：P1 陈旧二进制掩盖的 paint 契约测试、P2 ramp 探针 NaN 污染、P2 变异缺口、9 P3 → 全部修复/处置 | O-A1 七族两遍绿 + sar/terrain 全家族 + 12 受影响族两遍绿 | 通过 | push + PR |
| 6 | M7 | push 分支 + gh pr create #1676（74 文件 100% 白名单，MERGEABLE） | gh 核实 | 通过 | 终报（本文件） |
