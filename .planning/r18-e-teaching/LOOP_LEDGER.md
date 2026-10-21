# R18-E LOOP LEDGER

启动打印: `[GOAL Loop] R18-E: a60c8831c9cc4e64701d17bde2ace67a74a9b7a0 | Oracle A-G | WP-0 基线 | 迭代 0`

| # | 阶段/任务 | 假设 | 改动文件 | 验证命令 | 退出码 | 结果 | 下一步 |
|---|-----------|------|----------|----------|--------|------|--------|
| 0 | Recon | 三路审计可定位真实缺口 | (无) | gh pr list / overlap_scan / narrow_targets | 0 | 1×P0(D1) + 7×P1 + 18×P2/P3 候选; 详见 BASELINE.md | 建基线 |
| 1 | WP-0 | 依赖路径可复用 r16-a 缓存 + Catch2 离线源 | (无) | cmake 配置×5 (PROJ→GSL→Catch2→NETCDF) | 0 | 配置成功: build-r18-e Ninja+Debug, vendor prefix+系统 Qt | 启动 RED 构建 |
| 2 | WP-E01~E07 | 22 项缺陷可先写最小失败测试再统一捕获 RED | tests/test_r18_e_*.cpp ×9 + tests/CMakeLists.txt 注册 + 3 个脚手架字段 (refusedEntries/ok/verifierDeclUnevaluated) | 构建中 (flock+guard, -j2) | - | RED 构建分片重试中 (3749 步, 首片被宿主 10min 截断, ninja 增量续跑) | 捕获 RED → 应用修复 |
| 3 | WP-修复 | 24 项源修复按 FIX_DESIGNS.md 应用（teaching/lab/verification/grader/faultlab/help/studio/teaching_admin/cli） | 21 个源文件 | (见 GREEN 构建) | - | 首轮: 26 RED → 10；修正: 密度不变量(contiguous+max==lastSeq)、AtMost 精确镜像(value>windowFrom)、测试补 grader transcript、V2 计数语义、V4=4 checks | 下一分片 GREEN |
