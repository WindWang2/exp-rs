# DECISIONS — R23-D 决策日志

| # | 决策 | 理由/证据 |
|---|---|---|
| D1 | CT-6 修复 = 新增 foundry 校准规则 `ndvi_landsat.rules.json`（新 lab id），不改动 fixture 权威 `ndvi_basics.rules.json` | test_lab_grading 的 reference/wrong-answer 语料把 fixture 校准钉死（reference=100）；bundle overlay 已确立"每场景一权威"的分裂设计（其文件自述 "the repo copy tracks the D4/D1 fixture"）；改旧规则必炸语料，放宽容差被协议禁止 |
| D2 | e2e 回归用进程内 foundry（sicnu_sample_foundry 库）+ 生产核心 runSpectralIndexCore + OutputVerifier，不起 CLI 子进程 | test_sample_fixtures 先例（同库直链）；foundry 确定性契约（ADR 0164，字节级同宿主复现）；runSpectralIndexCore 即注册算子的生产路径（rs_spectral_index_operator.cpp:964 委托）；避免全量 CLI 目标的重链接成本 |
| D3 | WP-D2 用 catch (Json::Exception) 而非 return-false 假设 | vcpkg jsoncpp 1.9.6 reader.h:338-342 文档 + json_reader.cpp:1040 throwRuntimeError 现场证实；与 lab_pack/grader_json/recipes 的 catch 纪律对齐 |
| D4 | WP-D4 在运行时层加 isfinite 门，不改 JSON Schema | data/schemas/labspec.schema.json 不在本轨白名单；JSON Schema 无可移植的 double 有限性断言；grader_types 的 isfinite 纪律即运行时层先例；data/labs 全目录扫描 0 个非有限字面量（无既有文档受影响），expected_numeric 目前无生产使用 |
| D5 | #1648/#1653/#1593/#1598/#1599/#1601/#1602/#1610-1613/#1678/#1694 全部按 FIXED_OPEN 处理，只跑回归证明，不重修 | 协议 §0/§4 明令禁止重实现已修问题；修复提交现场定位（a1ffd5c66、51eb7d1a6 等）；防 merge-loss 靠回归矩阵（WP-D5） |
| D6 | D01/D03/D05-D07/D09-D11 大面积普查/重写 DEFERRED | Explore 全模块普查：0 TODO/FIXME、NaN/深度/路径逃逸/确定性已系统性修复且有注释可考；协议禁止无根据臆造缺陷；若 R1/R2 或基线测试出红再升级 |
| D7 | 构建 -j2 不升 -j4 | 现场内存 78% 已用（6.9GB 空闲/31.2GB），协议 RSS>70% 条款生效；无并发竞争（本轨独占 build-D） |
| D8 | vcpkg 依赖 3GB 复制进本 worktree build-D + VCPKG_MANIFEST_INSTALL=OFF | 避免污染主仓 build-dev 缓存（协议禁污染兄弟缓存）；本地 vcpkg checkout 恰在 pinned baseline；二进制缓存与网络均可用作兜底 |
| D9 | 测试注册追加在 tests/CMakeLists.txt 文件末尾独立块 | #1714→#1715 事故教训：共享文件只 append-only；末尾独立块最小化三方合并冲突面 |
