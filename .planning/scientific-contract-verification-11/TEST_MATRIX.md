# TEST_MATRIX — scientific-contract-verification-11

每项能力 → 独立 oracle → 命令 → exit → evidence。持续追加；最终每行须有两次连续运行记录。

| # | 能力 | 独立 oracle | 命令 | exit | evidence |
|---|---|---|---|---|---|
| T-1 | (见 evidence 列) | test_contract_census_11 | 0 | All tests passed (768 assertions in 7 test cases) RUN_EXIT:0 |
| T-2 | (见 evidence 列) | 同上（override-truth gate） | 0 | ✓ 768 断言内 |
| T-3 | (见 evidence 列) | test_contract_determinism_11 | 0 | All tests passed (138 assertions in 4 test cases) RUN_EXIT:0 |
| T-4 | (见 evidence 列) | test_verification_metamorphic_11 M1 | 0 | metamorphic 1122 ✓ |
| T-5 | (见 evidence 列) | M2 band-reorder | 0 | 1122 ✓ |
| T-6 | (见 evidence 列) | M5 identity CRS | 0 | 1122 ✓ |
| T-7 | (见 evidence 列) | （zonal 未纳入；M3/M6 替代） | 0 | 1122 ✓ |
| T-8 | (见 evidence 列) | M4 NoData 单调 | 0 | 1122 ✓（并抓到 band_ratio 真缺陷，已修） |
| T-9 | (见 evidence 列) | temporal 时间平移 | 0 | not-implemented-with-reason（fixture 成本；见 CAPABILITY_MATRIX） |
| T-10 | (见 evidence 列) | test_mutation_kill_11 | 0 | All tests passed (42 assertions in 3 test cases) RUN_EXIT:0 |
| T-11 | (见 evidence 列) | numeric_reference NDVI/SAVI | 0 | All tests passed (234 assertions in 5 test cases) RUN_EXIT:0 |
| T-12 | (见 evidence 列) | 辐射定标链 | 0 | 部分（known_answer_corpus 105 ✓ 覆盖 DN→radiance/TOA/bt） |
| T-13 | (见 evidence 列) | Welford/分位数 | 0 | known_answer_corpus 105 ✓（既有闭式） |
| T-14 | (见 evidence 列) | translate/clip/threshold 解析 | 0 | numeric_reference 234 ✓ |
| T-15 | (见 evidence 列) | failure F1/F2/F3 | 0 | All tests passed (32 assertions in 5 test cases) RUN_EXIT:0 |
| T-16 | (见 evidence 列) | F4 cancel | 0 | 32 ✓ |
| T-17 | (见 evidence 列) | F5（不存在目录=确定性；只读=WARN 记录） | 0 | 32 ✓ |
| T-18 | (见 evidence 列) | help↔registry | 0 | cross_surface 361 ✓ |
| T-19 | (见 evidence 列) | agent↔registry/tool surface | 0 | 361 ✓ |
| T-20 | (见 evidence 列) | graph 快照 | 0 | platform_9 预存 3 失败（duplicate findings，master 数据）；cross_surface 361 ✓ 绑定 132 rs 记录 |
| T-21 | (见 evidence 列) | 10.0 gates | 0 | scientific_contract_10 1480 ✓ / science_10 1187 ✓ / drift_10 预存失败 |
| T-22 | (见 evidence 列) | ladder L0-L2 | 0 | L0 ok / L1 ok / L2 部分通过（预存 4 失败），JSON 报告入库 |
| T-23 | (见 evidence 列) | readiness | 0 | collect_readiness 再生（Windows exe 解析） |
