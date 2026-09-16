# CURRENT_ARCHITECTURE — cn-eo-product-physics-11（基线 a5b11b7f 现状）

## Authority 图

```
data/products/sensor_profiles/{gaofen,zy3,zy1,hj}.json   ← 唯一 sensor 真值 (ADR 0147, schema v1)
        │  loadSensorProfile() fail-closed; sensorProfileKeys() 发现已知警告跳过
        ▼
src/geospatial/products/sensor_profile.{h,cpp}           ← registry loader（版本门 + unknown-key 报告）
        │
src/geospatial/products/cn_product_metadata.{h,cpp}      ← CN 身份识别（kSupportedPatterns 17 条 +
        │                                                   unsupportedFamilyReason 显式拒绝表）
        │                                                   CRESDA sidecar XML expat 白名单扫描
        │                                                   （legacy MetaInfo / current ProductMetaData
        │                                                   / cresda_unknown_root + unknown_top_level）
        ▼
src/geospatial/products/cn_product_adapters.cpp          ← 每族一个 ProductAdapter（gaofen/zy3/zy1/hj）
        │
src/geospatial/products/product_registry.{h,cpp}         ← ProductAdapterRegistry：Landsat/S2/S1/MODIS
        │                                                   + 4×CN + GenericRaster fallback
        │                                                   Completeness: Complete/PartialReadable/
        │                                                   Invalid/UnsupportedVersion
        ▼
src/operators/rs/rs_product_import_plan.{h,cpp}          ← 唯一导入 seam：
        │    planCnProductImport / evaluateCalibration / executeCnProductImport
        │    identify→inspect→validate→resolve constituents→role map→optional calibration
        │    →stack(SatelliteProducts::stackToGeoTiff)→stamp→provenance
        ├── src/operators/rs/rs_cn_product_import_operator.cpp   rs:cn_product_import
        ├── src/operators/rs/rs_gaofen_import_operator.cpp       rs:gaofen_import
        ├── src/operators/rs/rs_zy3_import_operator.cpp          rs:zy3_import
        ├── src/operators/rs/rs_hj_import_operator.cpp           rs:hj_import
        ▼
surfaces: src/app/dialogs/product_import_dialog.*  (GUI, 消费 plan/result JSON)
          src/cli/cli_commands.cpp                 (CLI)
          src/agent/spatial_tools/io_tools.cpp     (agent; ProductAdapterRegistry 消费)
```

## 关键契约（不得破坏）

- **Absence is absence**：sidecar 未声明的字段显式 missing，绝不默认（ADR 0157 DECISIONS D-06）。
- **Fail-closed registry**：缺文件/schema 违规 = 结构化错误；unknown key = ignored + 报告。
- **Calibration 全有才应用**：radiance = DN×gain+bias 仅当每个请求波段都有 gain+bias；部分覆盖 = typed refusal。
- **pan/MS 选择**：registry `pan_variant` 链接 + sidecar 形状（ModeID=PAN 或 1-band inventory），不从 band 编号猜。
- **Completeness**：missing core constituent → PartialReadable；不伪装完整导入。
- **A-0065 band-role 词汇**：unknown role 必须带 role_reason。
- **metadata stamps**：`SICNU_*` 命名空间（见 docs/products/cn-satellites.md 字段表）。

## 已知缺口（本 track 的靶子）

1. registry v1 无逐字段类型/单位 guard（10.0 deferred）。
2. GF-3/GF-4/GF-5/ZY-1 02B/02D/02E/CBERS = 识别但拒绝（reason 硬编码在 unsupportedFamilyReason）。
3. 无高光谱 band-axis 机制（330 band 无法在 v1 bands 数组表达——技术上可以但无生成式支撑/bad-band 语义）。
4. 无 INPE/CBERS 第三 generation parser。
5. ImportPlan 无 dry-run/constituent graph/checksum/cancel 检查点可见性。
6. GUI/CLI/agent 不展示 per-constituent 诊断图。
7. fixtures 分散在测试内嵌字符串中，无 corpus/golden 结构。

## 线程/生命周期注意

- ProductAdapterRegistry：进程级单例，首次使用后线程安全。
- expat 扫描器无全局状态；listDirectoryBounded 上限 512。
- RSOperatorContext 承载 cancel/资源语义（Phase 4 接入点）。
