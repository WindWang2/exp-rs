# TEST_MATRIX — cn-eo-product-physics-11

每项能力 → 独立 oracle → 命令 → exit → 证据。Oracle 一律独立于被测实现（golden JSON / 手算 known-answer / 独立解析）。

| # | 能力 | Oracle | 命令 | exit | 证据 |
|---|---|---|---|---|---|
| T-01 | registry v1 回归（现有行为不破坏） | 既有 `test_cn_products` 全绿 | `ctest -R test_cn_products -j1` | — | — |
| T-02 | registry v2 严格验证（合法文件通过） | validator 无 warning/error | 同上（新增 CASE） | — | — |
| T-03 | registry v2 拒绝损坏（逐字段） | 故意损坏 fixture → 指名道姓的错误 | 新 `test_sensor_schema` | — | — |
| T-04 | pan/ms 交叉引用 drift | validator 捕获悬空 variant 引用 | 同 T-03 | — | — |
| T-05 | GF-3 识别 + metadata | golden JSON（手写自公开规格样例） | 新 `test_cn_product_families11` | — | — |
| T-06 | GF-3 未知 generation 拒绝 | UnsupportedVersion + diagnostics | 同 T-05 | — | — |
| T-07 | GF-4 识别 + GEO 语义 | golden JSON | 同 T-05 | — | — |
| T-08 | GF-5 AHSI 识别 + 330-band axis | 波段数/首末波长/FWHM 对公开表 | 同 T-05 | — | — |
| T-09 | GF-5 subdataset inventory（driver 缺失 typed 拒绝） | 错误码 + 消息 | 同 T-05 | — | — |
| T-10 | ZY-1 02B/02D/02E 识别 + 模式 | golden JSON | 同 T-05 | — | — |
| T-11 | CBERS INPE generation 解析 | golden JSON（独立 parser） | 同 T-05 | — | — |
| T-12 | CBERS 未知变体拒绝 | unsupported reason | 同 T-05 | — | — |
| T-13 | HJ rpc_rpb constituent 诊断 | missingDeclaredFields/rpc 报告 | `test_cn_products` 新 CASE | — | — |
| T-14 | ImportPlan dry-run constituent graph | graph JSON golden（含 checksum） | 新 `test_product_import_plan11` | — | — |
| T-15 | 只读 source 目录导入 | 无写操作 + typed 报告 | 同 T-14 | — | — |
| T-16 | 中文路径全链 | import 成功 + metadata 正确 | `test_cn_products`（回归） | — | — |
| T-17 | 取消 → 零半成品 | cancel 后输出文件不存在 | 同 T-14 | — | — |
| T-18 | 校准部分覆盖 refusal | typed 错误点名波段 | `test_cn_products`（回归） | — | — |
| T-19 | GUI dialog 新诊断展示 | offscreen 测试断言 | `ctest -R test_product_import_dialog -j1` | — | — |
| T-20 | agent product plan 工具 | 工具输出 JSON golden | 新 agent tool test | — | — |
| T-21 | e2e：新 family identify→plan→import→stamps | SICNU_* 元数据 known-answer | e2e CASE ×N family | — | — |
| T-22 | 高光谱 stack 波长 stamps | 每 band WAVELENGTH = registry 值 | 同 T-21 | — | — |
| T-23 | fixture corpus 完整性 | golden 目录清单 drift gate | validator test | — | — |

运行约定：`QT_QPA_PLATFORM=offscreen`、`ctest -j1`、targeted 先行。
