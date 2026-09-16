# REVIEW_LOG — cn-eo-product-physics-11

## Round 1 — 主 agent 全 diff review（Phase 6 前，2026-09-15）

| Finding | Severity | Disposition | Commit/Test |
|---|---|---|---|
| dry-run 期间测试环境语义（fixture root 指向 sensor_profiles 本身，loader 期望 data root） | P3(测试) | 修复 makeDataRoot/writeRegistry 语义 | a8cffb80 / test_sensor_schema 98→123 断言全绿 |
| INFO 宏在 catch 块内的 Catch2 展开冲突（MSVC C2512） | P2(测试) | 三个测试文件改为消息捕获式断言 | a8cffb80 等 / 全套件编译通过 |
| test_sensor_schema raw string 被内容中 `)"` 提前终止 | P2(测试) | 全部改用 R"json(...)json" 分隔符 | a8cffb80 / 编译通过 |
| 新测试缺 using namespace sicnu::geo | P2(测试) | 补 using 指令 | a8cffb80 / 编译通过 |
| families11 断言笔误（GF-3 completeness、HR 波段数、JSON 数组取值） | P3(测试) | 逐项修正 | 874c6800 / families11 全绿 |
| **stackToGeoTiff 取消异常穿透清理路径**：IO-failure return 会删半成品，但 progress bridge 抛出的 Cancelled 异常绕过全部清理 → 半成品 GeoTIFF + GDAL 句柄泄漏（Windows 文件锁） | **P1** | 双层修复：satellite_products.cpp band 循环 try/catch（close+remove+rethrow）；rs_product_import_plan.cpp 同语义守卫 | 4bd37616 / plan11 mid-stack cancel 测试 |
| writeCnImportMetadata 失败路径留下无 stamp 的栈文件 | P2 | 打点失败即删输出 | 4bd37616 |
| readFileText 用窄字符 ifstream 打不开中文目录 | **P1**（Oracle #3 中文路径） | 改 fs::u8path 打开 | 874c6800 / plan11 中文路径用例 |

## Round 2 — 独立 adversarial review（subagent #2，只读，2026-09-16）

Reviewer: 独立 general-purpose agent（只读约束，git diff a5b11b7f..HEAD 全量审阅）。
Verdict: **P0=1 P1=4 P2=4 P3=5**（全部有代码证据；另附 verified-clean 清单）。

| # | Finding | Severity | Disposition | Test |
|---|---|---|---|---|
| R-01 | product_import_dialog.cpp 字符串字面量内裸换行（heredoc 转义被吞）→ GUI 目标无法编译 | **P0** | 已修：换行改为转义 `"\n"`（QStringLiteral 常量 + tr 字面量×2） | test_product_import_dialog 73/7 PASS |
| R-02 | sensor_profile.cpp 对 wrong-typed JSON 字段调用 asString() 抛 Json::LogicError，穿透 validator 的 report-not-throw 契约 | **P1** | 已修：parseSensorEntry 边界 trampoline 把 Json::Exception 转 typed GeoError；validator pass-2 cross-ref 读 pan/ms_variant 加 isString 守卫 | test_sensor_schema 123 断言含契约用例 |
| R-03 | v2 "严格类型"未接线：positivePhysical() 零调用；字符串型数字/缺 modality 静默当作缺省 | **P1** | 已修：validateStrictEntryV2/BandV2 增加 isString/isNumeric/显式 modality/gsd_m positivePhysical 检查；新增 5 个负测试用例 | test_sensor_schema v2 matrix |
| R-04 | 校准循环（GA_Update）中取消仍留 DN/radiance 混合半成品 + 句柄泄漏 | **P1** | 已修：校准段 try/catch（close+remove+rethrow），plan11 新增 mid-calibration cancel 用例 | plan11 220 断言含新用例 |
| R-05 | cbers4_pan10 波段范围 0.51-0.73µm 与 CBERS-4 发布规格 510-850nm 不符 | **P1**（数据错误） | 已修：0.51-0.85 / 680nm，note 注明来源（NASA CMR/CEOS） | drift gate 复跑通过 |
| R-06 | gaofen.json/zy1.json 文件级 source 未覆盖新增 family | P2 | 已修：两条 source 扩写（GF-3/4/5、02B/02D/02E + AHSI 波长来源策略） | validator 0 findings |
| R-07 | ADR 0159 宣称的 AHSI GDAL subdataset inventory 未实现，HDF 分发降级语义误导 | P2 | 收窄范围（DECISIONS D-11）：本 track 只适配 TIFF-backed 包；ADR 决议 4 改写；适配器 missingConstituents 注记指明 HDF 为 declared follow-up | families11 注记断言 |
| R-08 | CLI/agent 非法/负 hash-budget 静默产出无意义校验报告 | P2 | 已修：CLI --hash-budget 解析失败/≤0 → InvalidInput；agent 工具非正整数 → typed failure | 代码审查（无自动测试，行为简单） |
| R-09 | GUI 预检在 UI 线程同步哈希 256MiB/文件 | P2 | 已修：对话框预检预算降至 8 MiB 并以 digest_scope 标注（CLI/agent 保持默认 256MiB） | test_product_import_dialog PASS |
| R-10 | GF3- 连字符命名失去具名拒绝理由 | P3 | 已修：unsupportedFamilyReason 恢复 GF3- 分支 | families11 refusals |
| R-11 | CBERS bandid 数值未归一化为 B\<n\>（与逗号表不对称） | P3 | 已修：数值 BandID 归一化 | 代码审查 |
| R-12 | parseSpectralRangeUm 接受 "0.45-0.52-0.60"（stod 前缀解析） | P3 | 已修：两端全消费校验 | test_sensor_schema（未新增专例，规则收紧无回归） |
| R-13 | band_axis.count 接受小数（jsoncpp isIntegral 对范围内实数为真） | P3 | 已修：分数 count 拒绝 | 代码审查 |
| R-14 | readable 字段 doc 与空文件行为不符；测试三元冗余 | P3 | doc 已修；测试简化 | — |

结论：**P0=0、P1=0（R-01..R-05 全部修复并重跑验证）**；R-06..R-14 全部 disposition（9 修 1 收窄记录）。
复验：修复后 9 套件全绿（4011 assertions：123+768+282+220+1378+38+46+479+73）。
