# F02 · Chinese EO Product Physics & Import Platform 11.0

**Baseline:** `origin/master @ a5b11b7f` (fix: fail-closed fixes for review issues #994–#999 (#1000)) · **Branch:** `zcode/cn-eo-product-physics-11` · Worktree `../exp-rs-cn-eo-product-physics-11`

## 与启动时 open PR 的 dedupe / ownership

启动审计（2026-09-15）时快照中的 #991/#992 已合入 master；唯一并发 PR 为 #1008
`zcode/radiometric-spectral-workbench`（CONFLICTING）。其 changed files（`src/core/radiometric_state.*`、
`src/analysis/{atmospheric,hyperspectral}`、`src/processing/algorithms/radiometric_calibration.*`、
`spectral_*`、`src/app/widgets/spectral_*` 等）对本 track **read-only**：本 track 不做通用辐射/
大气校正 kernel（那是 #1008 的业务主体），产品层只传递 declared coefficients（gain/bias verbatim
+ provenance），不 import 其 `exp_radiometric` API；ADR 编号从 0159 起避让其 0158。
Phase 8 复查发现 #1009/#1010/#1011（execution/mosaic/classification）均与本 track 无业务文件交集，
仅共享 `CHANGELOG.md`/`tests/CMakeLists.txt`/`.gitignore` 的 append-only 接线（手工并段即可）。
详见 `.planning/cn-eo-product-physics-11/PARALLEL_OWNERSHIP.md`。Open issues #1001–#1007
均为 dataset/workflow/georef/io:clip 领域，与本 track 无交集（逐条 dedupe 记录于 BASELINE.md）。

## 架构决定（ADR 0159 + DECISIONS.md）

- **Registry schema 2.0 additive**：v1 文件按历史规则可读，v2 逐字段严格验证（必填身份字段、
  closed modality/ADR-0065-role 词汇、物理量有限正值、波段 id 大小写不敏感唯一、
  wavelength↔spectral_range 一致性）；registry 级 validator（`validateSensorProfiles()`）
  report-not-throw；committed registry 由 drift gate 钉死为 0 findings。
- **高光谱波段轴落盘，绝无运行时公式生成**：GF-5 AHSI（330）与 ZY-1 02D/02E AHSI（166）波段
  全量写盘；`band_axis` 块只描述 extent/ordering/bad-band ids；AHSI 逐波段波长一律不伪造，
  只从产品自身声明元数据运输。
- **一个 generation 一个 parser，schema 不混用**：CRESDA optical（现状）／CRESDA SAR（GF-3，
  极化 HH/HV/VH/VV 词汇+未知 token 上报+无 σ⁰ 宣称）／CBERS INPE（第三 generation，未知根
  typed 拒绝）。
- **ImportPlan dry-run 只读** + constituent graph + 每文件 sha256（超预算 = 显式标注的前缀摘要，
  绝不冒充全文件摘要）；取消零半成品契约双层加固。
- **Surface 单一服务**：CLI `data product plan`、agent `io:product_plan`、GUI cn 预检摘要均消费
  同一 `dryRunCnProductImport`，无逻辑复制。

## 实际交付

| WP | 交付 |
|---|---|
| A | schema 2.0 + validator + drift gate + `docs/products/SENSOR_SCHEMA.md` |
| B | GF-3 SAR（declared-metadata 级，SAR stamps `SICNU_POLARIZATIONS`/`SICNU_ORBIT_DIRECTION`）、GF-4 PMI（pan/MS registry 链） |
| C | GF-5 AHSI 330-band 轴聚合（1 个 measurement asset + axis 注记）；波长运输策略（见 Known limitations） |
| D | ZY-1 02B CCD/HR、02D/02E PMS(+pan)/AHSI 166-band 轴 |
| E | CBERS-4 MUX/WFI/PAN10 INPE generation + HJ/CRESDA 诊断延续；refined refusals（GF-4 红外、GF-5 VIMS/GMI/EMI/SATS、ZY-1 IRS、CBERS 其他） |
| F | dry-run + checksums + 取消零半成品（stack/calibration 双路径）+ 只读源 + 中文路径（readFileText u8path 修复） |
| G | CLI/agent/GUI parity（同一服务；对话框摘要走状态栏——预览树顶层行与 `m_preview.children` 一一映射，不可扩展） |
| H | `tests/fixtures/cn_products/`（合法/损坏/缺身份/多 generation + golden metadata + manifest drift gate；TIFF 运行时合成，零真实影像） |

新增注册数据：`gaofen.json`（gf3、gf4_pmi(+pan)、gf5_ahsi）、`zy1.json`（02B/02D/02E 共 8 键）、
新文件 `cbers.json`（3 键）——全 36 键过 v2 严格验证（本机脚本复核 + validator drift gate）。

## 兼容性

- master 测试契约更新仅为 refusal 清单重划（4 个 now-supported 移除、新拒绝样本补充），其余
  1,325 断言原样通过；4 个既有算子/GUI/CLI surface 签名不变（dry-run 为 additive API）。
- registry v1 文件行为保持；v2 门 `{1,2}` 外版本仍 typed 拒绝（既有 version-99 测试保持通过）。

## 本地测试证据（Local evidence only; no online CI dependency）

构建：VS2022 MSVC + Ninja `-j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`，冷构建期 cl.exe RSS ≈
140–430MB×2，远低于 70% 阈值；Git Bash 无 load average，按协议记录 not-executed 并恒定 -j2）。
离线 configure：Catch2 v3.7.1 经 `FETCHCONTENT_SOURCE_DIR_CATCH2=C:/deps/catch2-src`（同版本
本地副本，规避网络克隆失败），其余沿用 `scripts/windows/setup.cmd` 官方环境（`C:\deps` 工具树）。

终验（Phase 8，**连续两遍全绿**，`QT_QPA_PLATFORM=offscreen`、`ctest` 单进程语义、
`PROJ_DATA` 指向宿主 vcpkg proj.db——纯环境量，已对照证明与 diff 无关）：

| Suite | Assertions/Cases | Pass 1 | Pass 2 |
|---|---|---|---|
| test_sensor_schema（新） | 123 / 7 | ✅ | ✅ |
| test_cn_product_families11（新） | 768 / 9 | ✅ | ✅ |
| test_cn_product_fixtures（新） | 282 / 3 | ✅ | ✅ |
| test_product_import_plan11（新） | 220 / 7 | ✅ | ✅ |
| test_cn_products（回归） | 1,378 / 29 | ✅ | ✅ |
| test_io_products（回归） | 38 / 4 | ✅ | ✅ |
| test_io_product_registry（回归） | 46 / 8 | ✅ | ✅ |
| test_satellite_products（回归） | 479 / 20 | ✅ | ✅ |
| test_product_import_dialog（回归） | 73 / 7 | ✅ | ✅ |

`git diff --check origin/master...HEAD` clean；冲突标记/secret 扫描 clean；无生成物入 diff。

## Review

主 agent 全 diff review（Round 1：8 项，含 stackToGeoTiff 取消异常穿透清理的 P1）+ 独立只读
adversarial review（Round 2：**P0=1 P1=4 P2=4 P3=5**，全部有代码证据）。处置：P0/P1 全修并
重跑验证；P2 全修（AHSI subdataset 收窄为 declared follow-up，DECISIONS D-11）；P3 全修。
逐条 disposition 见 `.planning/cn-eo-product-physics-11/REVIEW_LOG.md`。

## Known limitations / follow-ups

- **HDF5-backed AHSI 分发**（GDAL subdataset inventory + 驱动缺失 typed 拒绝）为 declared
  follow-up（D-11）；本 track 契约覆盖 TIFF-backed 包，HDF 产品目前报
  `missingConstituents`（注记指明 follow-up），绝不猜测导入。
- GF-4 仅适配 PMI（公开规格 50m MS+pan）；GF4_PMS 等命名保持具名拒绝。
- AHSI 逐波段中心波长/FWHM 不入库（无在库可核来源），导入时从产品自身声明元数据运输；
  注册表波段 role=unknown + role_reason。
- 独立 review 提出的 empty-file `readable` 语义、tests env 泄漏（失败路径 qunsetenv 不执行）
  等已按 P3 处置（doc 修正/简化），无行为影响。

## Out-of-scope 发现

无 P0 级范围外发现。（spectrum: #1008 的 6S/大气校正与其测试与产品层无耦合；io:clip CRS
问题属已开 issue #1001，非本 track 领域。）

## Commits

`874c6800` families · `4bd37616` import-plan/dry-run/parity/fixtures · `a8cffb80` schema 2.0 ·
`58637f13` integration registrations · `faddf0b2` review-round fixes · +planning docs

**Local evidence only; no online CI dependency.**
