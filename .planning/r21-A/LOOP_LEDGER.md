# LOOP_LEDGER — r21-A

| 轮 | 里程碑 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|---|
| 1 | M0 | LIVE 盘点、worktree 建立、5 份基线文档 | git/gh 输出存档 | 通过 | 基线构建 + 缺陷勘查 |
| 2 | M0 | worktree+基线文档；基线构建踩坑：vendored GDAL 带 flang wrapper2_* 未定义符号（stub 方案）、SicnuSharedLinkGuard+新 binutils 需 shared 也加 --allow-shlib-undefined、vendor prefix 静态 libexpat/sqlite3/zlib/zstd 需 pin 系统 .so | BUILD_EXIT=2→定位→修复配置 | 通过（构建重启） | 缺陷修复批次 M2 |
| 3 | M2 | 16 项缺陷修复完成（15 采纳+1 驳回），9 个原子提交；构建修好 join 编译错后续建 | 待测试验证 | 待验证 | GREEN×2 波次 |
