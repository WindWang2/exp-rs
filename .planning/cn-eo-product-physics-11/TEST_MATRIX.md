# TEST_MATRIX — cn-eo-product-physics-11

每项能力 → 独立 oracle → 命令 → exit → 证据。Oracle 一律独立于被测实现（golden JSON / 手算 known-answer / 独立解析）。

| # | 能力 | Oracle | 命令 | exit | 证据 |
|---|---|---|---|---|---|
| T-01 | registry v1 回归（现有行为不破坏） | 既有 `test_cn_products` 全绿 | build-dev: `./test_cn_products.exe` | 0 | 1378 assertions/29 cases PASS（refusal 清单按 ADR 0159 契约更新） |
| T-02 | registry v2 严格验证（合法文件通过） | validator 无 warning/error | `./test_sensor_schema.exe` | 0 | 98 assertions/7 cases PASS |
| T-03 | registry v2 拒绝损坏（逐字段） | 故意损坏 fixture → 指名道姓的错误 | `test_sensor_schema` v2 matrix | 0 | 12 腐坏用例逐字段错误消息 |
| T-04 | pan/ms 交叉引用 drift | validator 捕获悬空 variant 引用 | `test_sensor_schema` crossref | 0 | PASS |
| T-05 | GF-3 识别 + metadata | golden JSON（手写自公开规格样例） | `./test_cn_product_families11.exe` | 0 | 768 assertions/9 cases PASS |
| T-06 | GF-3 未知 generation 拒绝 | UnsupportedProduct + CRESDA 诊断 | 同 T-05 + fixtures corrupt | 0 | PASS |
| T-07 | GF-4 识别 + pan/MS 语义 | registry pan_variant 链 | 同 T-05 | 0 | PASS |
| T-08 | GF-5 AHSI 识别 + 330-band axis | band_axis count/ordering（无伪造波长） | 同 T-05 + golden | 0 | PASS（无波长声明=通过设计） |
| T-09 | GF-5 导入波长运输策略 | 只运输声明值，不 fabricate | 同 T-05 | 0 | 波长从产品自身元数据运输（DECISIONS D-03） |
| T-10 | ZY-1 02B/02D/02E 识别 + 模式 | golden JSON | 同 T-05 | 0 | PASS |
| T-11 | CBERS INPE generation 解析 | golden JSON（独立 parser） | 同 T-05 + golden/cbers4 | 0 | PASS |
| T-12 | CBERS 未知变体拒绝 | unsupported reason + unknown-root refusal | 同 T-05 | 0 | PASS |
| T-13 | rpc/sibling constituent 诊断 | plan 中 rpc/sibling 字段 | `test_product_import_plan11` dry-run graph | 0 | 3 constituents 全报 |
| T-14 | ImportPlan dry-run constituent graph | 独立 sha256 oracle（one-shot 对流式） | `./test_product_import_plan11.exe` | 0 | 220 assertions/7 cases PASS |
| T-15 | 只读 source 导入 | 权限+字节不变断言 | 同 T-14 | 0 | PASS |
| T-16 | 中文路径全链 | dry-run+execute 成功 | 同 T-14（产品目录/高分一号） | 0 | PASS（readFileText u8path 修复后） |
| T-17 | 取消 → 零半成品 | cancel 后输出文件不存在 | 同 T-14 mid-stack cancel | 0 | PASS（satellite_products 异常清理修复后） |
| T-18 | 校准部分覆盖 refusal | typed 错误点名波段 | `test_cn_products` 回归 | 0 | 保持 |
| T-19 | GUI dialog 新诊断展示 | offscreen 测试断言 | `ctest -R test_product_import_dialog -j1` | — | — |
| T-20 | agent product plan 工具 | 工具输出 JSON golden | 新 agent tool test | — | — |
| T-21 | GF-3 e2e identify→plan→import→stamps | SICNU_PRODUCT_TYPE/POLARIZATIONS/ORBIT_DIRECTION | `test_product_import_plan11` gf3 case | 0 | PASS |
| T-22 | 高光谱 stack 波长 stamps | 每 band WAVELENGTH = registry 值 | 同 T-21 | — | — |
| T-23 | fixture corpus 完整性 | golden 目录清单 drift gate | validator test | — | — |

运行约定：`QT_QPA_PLATFORM=offscreen`、`ctest -j1`、targeted 先行。
