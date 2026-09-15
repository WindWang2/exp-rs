# PLAN — cn-eo-product-physics-11

基线 `origin/master @ a5b11b7f`。执行顺序按依赖排布；每 Phase ≥1 原子 commit；Phase 边界 rebase。

## Phase 0（完成）— 审计与落盘
- [x] 刷新 origin/master / PR / issue 事实 → BASELINE.md
- [x] #1008 文件级 ownership → PARALLEL_OWNERSHIP.md
- [x] 代码现状审计 → CURRENT_ARCHITECTURE.md
- [x] GOAL 原文落盘、planning whitelist 追加

## Phase 1 — WP-A registry schema 2.0（authority 层）
现有 v1 registry（`sensor_profile.{h,cpp}` + 4 个 JSON）只有松散类型保证（10.0 遗留 debt）。
1. `SensorProfileRecord`/`SensorBandProfile` 增加严格字段验证（类型/有限性/单位/可选性）；schema version 2：`version: 2` 必填、逐字段 guard、未知 key 仍 ignored-but-reported、v1 文件保持可读（version gate 现状已 fail-closed，v2 是 additive）。
2. 新增 `validateSensorProfiles()` 生成式 validator（校验全部 registry 文件 + 交叉引用：pan_variant/ms_variant 指向存在 key、band id 唯一、wavelength/fwhm/gsd 有限正值、role∈ADR0065 词汇或带 role_reason）。
3. drift tests：registry 文件 ↔ validator 全绿；故意损坏 fixture → 逐字段错误消息。
4. 单位/可选性 provenance：wavelength/fwhm/gsd 的 has-flag 语义在 schema 文档化（`docs/products/SENSOR_SCHEMA.md`，SCHEMA.md 角色由它承担）。

## Phase 2 — WP-B GF-3 SAR / GF-4 + WP-C GF-5/AHSI（第一大块）
- GF-3：identity pattern（`GF3_`/FSAR 等公开命名）→ sensor_profiles 新增 `gf3_*` 条目（C-band SAR，FSAR/IW/Scan 等 mode，Q/P 双极化词汇）→ sidecar：GF-3 公开 NAD/产品 XML 白名单扫描（satelliteid/polarization/imaging time/resolution…）→ `ProductMetadata.polarizations/modality=sar`；未知 generation → UnsupportedVersion；不做 sigma0 kernel（PARALLEL_OWNERSHIP：SAR kernels read-only）。
- GF-4：`GF4_` PMI/PMS 识别 + registry 条目（GEO 轨道语义 extra：sub-satellite point 扫描）。
- GF-5/AHSI：`GF5_` AHSI 识别 → registry 条目 330-band VOC/HSI 布局以生成式 band 轴表达（visible330 属性 + wavelength/fwhm 公式来源 provenance）→ HDF/子数据集 inventory（GDAL subdataset 枚举 + 逐 band 波长 FWHM stamps）→ stack/virtual access 走既有 stackToGeoTiff 契约；bad-band 清单 passthrough。
- 每项：identify→metadata→assets 的最小 vertical slice + known-answer/negative tests（fixtures 由 Phase 内 fixture helper 生成，见 WP-H 起步）。

## Phase 3 — WP-D ZY-1 02B/02D/02E + WP-E CBERS/HJ（第二大块）
- ZY-1 02B PMS/HR（宽照相机 WFI?按公开规格：02B CCD 19.5m/HR 2.36m pan）识别 + registry；02D/02E AHSI 高光谱：与 GF-5 同一高光谱 axis 机制复用（不复制代码）。
- CBERS：identity（`CBERS4_` 等）+ INPE generation（峰值不同 XML 根/字段）→ 第三 sidecar generation `cbers_inpe_*`，独立 parser 函数（10.0 DECISIONS D-02 已预留），与 CRESDA schema 不混用；不支持的 CBERS 变体保持显式拒绝。
- HJ：rpc_rpb 诊断完善（HJ 产品 .rpb 命名核对 + constituent 报告路径修正）；sidecar diagnostics 已有，补齐 missingDeclaredFields 对 HJ 特有字段的覆盖。
- 归一化拒绝语义：所有新增 family 的 unsupported 子模式（GF-5 其他载荷、ZY-1 IRS、CBERS 未知 generation…）逐条 reason。

## Phase 4 — WP-F ImportPlan dry-run/diagnostics + WP-G parity（surface）
- ImportPlan 增加 dry-run 模式（plan only, no pixel IO）+ constituent graph JSON（sidecar/image/rpc/sibling/checksum 状态）+ per-constituent checksum（输入只读、sha256 记入 provenance）+ 中文路径显式测试已在（复跑）；取消：RSOperatorContext cancel 检查点接入 stack 循环；原子输出：stack 失败/取消 → 删除半成品（沿用并核验 #960 离线原子语义）。
- partial-readable verdict 贯穿 dry-run 结果；read-only source 目录导入测试（Windows 语义注意）。
- GUI：product_import_dialog 展示 dry-run constituent graph/缺失项（消费 toJson，不在 UI 复制解析逻辑）；CLI：`products inspect --dry-run` 等价输出；agent：io_tools 增加 product plan 工具（复用同一服务）。
- capability meta + knowledge 页 + help catalog 同步（integration commit）。

## Phase 5 — WP-H fixture corpus + 稳健性硬化
- `tests/fixtures/cn_products/`：多 generation 合法/损坏/缺字段/中文路径/只读 fixtures（小型合成 TIFF + 手写 sidecar XML，golden metadata JSON）；生成 helper（确定性、离线）。
- 硬化：bounded listing 上限复验、文件读取失败语义、RPC/sibling 路径边界、cancel 清理、fail-closed 逐条负测试。

## Phase 6 — E2E / known-answer / drift
- 新 family 各 1 条 headless e2e（identify→plan→import→metadata stamps）+ GF-5 高光谱 stack 波长 known-answer。
- registry/fixture/capability 生成物 drift gate：validator + golden 对比。
- `ctest -R "cn_product|product_import|sensor_profile|satellite"` 全绿 ×1（第一遍）。

## Phase 7 — 独立 adversarial review
- 主 agent 全 diff review（origin/master...HEAD）→ 修复；
- subagent #2（只读 judge/general）独立 review → REVIEW_LOG.md，P0/P1 全修，P2 修或 disposition。

## Phase 8 — 终验 + PR
- rebase origin/master；`git diff --check`；冲突标记/secret 扫描；targeted gates **连续两遍**全绿；更新全部 planning 工件；push + `gh pr create`（PR_BODY.md）。
