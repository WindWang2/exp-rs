# DECISIONS — cn-eo-product-physics-11

- D-01 (2026-09-15) **Registry schema v2 additive**：v2 引入逐字段严格验证（类型/有限性/单位/可选性），v1 文件继续可读（version 门保持现状语义：未知大版本 fail-closed）。不重写 v1 文件为新格式（drift 最小、向后兼容）；validator 对 v1/v2 分别按其版本规则验证。
- D-02 (2026-09-15) **GF-5/ZY-1 02D/02E 高光谱 band 轴**：registry 允许 `bands` 数组直接展开生成式波段（对 AHSI 类 330-band 用 in-repo 生成脚本式 JSON——仍逐 band 落盘、无运行时公式推断），并新增 `band_axis` 描述块（count/orientation/wavelength 字段引用/bad-band 语义）。拒绝"运行时公式生成波长"——真值必须落盘可审计。
- D-03 (2026-09-15) **GF-5/ZY-1 02D HDF 语义**：产品识别/子数据集 inventory 支持基于 GDAL subdataset 元数据（运行时探测 driver）；driver 缺失 → typed 拒绝（依赖缺失不是 silent unknown）。Fixture 用 GTiff+sidecar 表达同一 metadata 契约以便离线测试。
- D-04 (2026-09-15) **CBERS INPE 第三 generation**：独立 parser 函数 + 独立 generation id（`cbers_inpe_*`），不与 CRESDA 白名单混用；不识别的 CBERS 根/字段组合 → UnsupportedVersion + bounded passthrough（沿用 10.0 D-02/D-06 方向）。
- D-05 (2026-09-15) **GF-3 SAR 范围**：仅 declared-metadata 级（identity/mode/polarization/几何/level/RPC constituent）；不做 sigma0/c imperialism 标定 kernel（SAR kernels 属 read-only 领域）。numeric domain 保持 digital_number + SAR 注记（沿用 10.0 D-03 语义）。
- D-06 (2026-09-15) **与 PR #1008 的边界**：不使用其 `exp_radiometric` API（open PR 非稳定 seam）；本 track 校准仍是 declared-coefficient 传递（gain/bias verbatim + provenance），物理换算 kernel 不在本 track 范围。ADR 编号从 0159 起避让。
- D-07 (2026-09-15) **fixtures 位置与形态**：`tests/fixtures/cn_products/**`（小合成 TIFF/手写 XML/golden JSON，全离线确定性），helper 以 C++ 测试工具函数提供；不引入 Python/外部生成器依赖。真实大影像不入库（GOAL 硬约束）。
- D-08 (2026-09-15) **ImportPlan dry-run**：`planCnProductImport` 本身即 dry-run（现契约不触像素）；新增的是 explicit `dryRun` 结果封装 + constituent graph/checksum/cancel 检查点，不改变现有函数签名语义（additive 重载/新函数），保证 4 个既有算子零破坏。
- D-09 (2026-09-15) **GUI parity**：product_import_dialog 只消费 plan/result JSON 渲染 constituent graph/缺失项（服务已在 master，UI 零逻辑复制）；CLI/agent 同一服务不同薄壳。
- D-10 (2026-09-15) **WP-G 收窄**（rescope 记录）：GUI/CLI/agent surface 均已存在，本 track 不新建 surface，只做新 family + 诊断 parity 接线（BASELINE.md 结论 3）。

## 争议裁决记录

- "sensor_profiles v2 直接重写 4 个 JSON 为 v2" vs "additive"：选 additive（D-01）——避免与未来并发 PR 的 registry 文件冲突、保持 diff 最小；v2 验证规则严格于 v1。
- "高光谱波长运行时公式生成" vs "落盘展开"：选落盘（D-02）——prompt Oracle #2 要求物理量有来源；公式属于生成工具，不属于运行时。

## Review-round dispositions (Phase 7, independent review)

- D-11 (2026-09-16) **AHSI subdataset inventory 收窄为 follow-up**：本 track 适配
  TIFF-backed AHSI 包（CRESDA 式 sidecar+TIFF 布局）；HDF5 分发及其 GDAL
  subdataset inventory 明确记为后续工作（ADR 0159 决议 4 已改写，CAPABILITY_MATRIX
  标 degraded/follow-up，适配器注记指明缺失成分），不再宣称未实现的 HDF 路径。
- D-12 (2026-09-16) **严格 JSON 类型是 v2 加载即拒绝语义**：present-but-wrong-typed
  字段（如 `"wavelength_nm": "485"`、`"satellite": 42`）= 指名道姓的 GeoError；
  jsoncpp LogicError 在 parseSensorEntry 边界统一转 typed GeoError，
  validateSensorProfiles 保持 report-not-throw 契约。
