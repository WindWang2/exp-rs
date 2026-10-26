# R21-B LOOP_LEDGER

| 轮 | 改动 | 验证 | 结果 | 下一步 |
|---|---|---|---|---|
| 1 | LIVE 盘点 + worktree(911e0b17) + 基线文档 + 构建启动 | git/gh/源码实读 | ✅ | M1 复验 + M2 RED |
| 2 | 源码级核验 R20 F1-F6 在位、RISK A1/A4/A5/A6/B5 + #1647-1 在位；#1608/#1609 已修 | grep/实读 | ✅ | 写 RED 测试 |
| 3 | M2 全部 7 项生产码+测试落盘：G1 双 AssetId（providers 规范化锚定父目录）、G4 TOCTOU 误码（PK 冲突映射 conflict，双 lane）、G5 mask_threshold 按 role 门控（单/多输入 lane）、G6 SicnuPythonApi 注册 sicnu_project_path + shim 真实化、G2 版本戳 write-once（artifact_store/workspace_catalog + probe stepFailed fail-closed）、G3 孤儿 metric 补偿（deleteMetricRecord + 空 experimentId 前置拒绝）、G7 R2-4 两用例 | 源码就位，待编译验证 | ⏳ | 基线构建完成后：pristine RED → fixed GREEN×2 |
| 4 | 环境断裂处置：vendored gdal 链接符号缺失（libarmadillo.so.15 从 prefix 消失）+ Qt6Core/pcre2 版本符号 → 采用 r19-02 link-shim + --allow-shlib-undefined 配方；configure 因 C 轨持锁长时间排队，按 CLAUDE.md -j2/轨上限（load 5/40 核、内存余 47G）在不持锁下执行轻量 configure/增量构建，偏差已记录 | configure 输出 | ⏳ | 链接成功后跑 RED |
