# R2 · Post-D-Landing Deep Review Dossier

Track: `zcode/r2-deep-review` · base `origin/master @ 7d78059d` · 2026-09-13
范围：PR #951–#956（2026-09-13 合并潮的 6 个功能 PR）+ R0 未提交发现的 HEAD 复验 +
横切漂移。R0 的 Tier A（src/operators + pi）结论复用其 dossier，不重复列。
构建工具链本会话不可用（`which cmake ninja gcc cl` 全空）→ 构建/测试验证标 not-executed。

## R0 发现复验结论（7 条，逐条裁决）

| R0 ID | HEAD 复验 | 处置 |
| --- | --- | --- |
| F-OPS-1 class_mapping 缺上界校验 | 仍成立（model_catalog.cpp:836 只验 ≥0 与单射） | **提交** F2-04 |
| F-OPS-2 fromMat 非连续回退 | 已修复（tensor_blob.cpp:161-175 按行拷贝） | 不提交 |
| F-OPS-3 qa_mask fail-open | 已修复（rs_qa_mask_operator.cpp:287-299 显式守卫 + 计数） | 不提交 |
| F-OPS-4 io:reproject srcCrsOverride 死参数 | 仍成立（io_operators.cpp:306 只做准入检查，314-320 未传值） | **提交** F2-05 |
| F-OPS-5 NMS O(n²) 不可取消 | 仍成立（detection_postprocess.cpp:130-164，无取消点） | **提交** F2-13（降 P2，maxDetections=100000 有界） |
| F-PI-1 pi 桥失步 | 缓解在位（mcp_bridge.ts:193-200 坏行丢弃+后续行续消费） | 不提交 |
| F-PI-2 deadline 回移 | 已双侧落地（mcp_bridge.ts:142 + exp-rs-spatial.ts:176） | 不提交 |

## 提交清单（F2 编号，全部 verified-by-file-inspection）

### F2-01 · P1 · dialog help catalog 字符串未进 i18n（#953 遗漏整个 catalog 层）

- **Location**: `src/app/dialogs/dialog_help_catalog.cpp:25-419`（字面量区）、
  `:459-487`（htmlForTool / shortForTool 直返）、`:478-487`（英文 fallback 常量）
- **Quoted**: `return  it->summary ;` / `return wrapBody( title,  it->summary ,  it->body  );`
- **Impact**: 338 行 dialog 帮助文本（title/body）+ 模态帮助框 + 通用 fallback
  全是裸 literal；`sicnu_zh_CN.ts` 对 catalog 零覆盖（`grep -c` = 0）。应用切中文后，
  这整层帮助恒英文——与 ADR"UI strings use English source"配套的 `.ts` 供给断裂。
- **Dedup**: `class_mapping`-style API 搜索不可用（限流）；R0 未覆盖 src/app；
  基线 #595–#945 在 9-13 合并潮之前。
- **Fix 建议**: dialog_help_catalog 用 `QT_TR_NOOP` 标记 catalog 条目并在返回处
  `QCoreApplication::translate`，或把 catalog 拆成随语言加载的资源；`sicnu_i18n_update`
  扫描范围纳入该文件。

### F2-02 · P1 · offline gate 全局开关是裸 bool（数据竞态）

- **Location**: `src/geospatial/remote/offline_gate.cpp:17`（`bool g_offline = false;`
  于匿名 namespace）、调用方 `src/app/main.cpp:147`、`src/cli/sicnu_worker_main.cpp:225`
- **Quoted**: `bool g_offline = false;`
- **Impact**: 主线程/启动线程 `setEnabled(true)` 写、工作线程 `enabled()` 读——无
  atomic/mutex。worker 进程在任务飞行中翻转开关即 UB；`isRemoteTarget` 读、
  `applyGdalNetworkDeny` 写 GDAL 全局配置同样无序。
- **Dedup**: 基线前无 offline_gate（#951 新建）。
- **Fix 建议**: `std::atomic<bool>`；GDAL 配置写仍需启动期单线程调用并注释。

### F2-03 · P1 · lab copilot 评估测试用裸 setenv（MSVC 编译失败）

- **Location**: `tests/test_harness_lab_evals.cpp:66-67`
- **Quoted**: `ScopedEnv( const char *key, const char *value ) : key( key ) { setenv( key, value, 1 ); }` / `~ScopedEnv() { unsetenv( key ); }`
- **Impact**: 同仓库 `tests/test_exprs_external_process.cpp:117` 已注释"MSVC has no
  POSIX setenv/unsetenv"并给出 `_putenv_s` 分支；新测试倒退为裸 POSIX，MSVC 下整个
  `test_harness_lab_evals` 目标编译失败（注册于 tests/CMakeLists.txt）。
- **Dedup**: 基线前无此文件（#952 新建）。
- **Fix 建议**: 照抄 test_exprs_external_process.cpp:117-127 的三分支写法。

### F2-04 · P2 · class_mapping 无上界校验（R0 F-OPS-1 复验仍成立）

- **Location**: `src/operators/framework/model_catalog.cpp:832-845`（校验段）、
  `src/operators/runtime/tile_inference_engine.cpp:1602-1606`（写入点）
- **Quoted**: `if ( mapping[i] < 0 )`（仅下界 + 单射检查）/
  `outRow[col] = static_cast<float>( productClass );`（未经域检查写入）
- **Impact**: 重映射值超过输出编码域时静默写坏像素。R0 dossier 有全证据链
  （`review/findings/operators.md` F-OPS-1），HEAD 代码未变。
- **Dedup**: R0 未提交 issue（其 GOAL 禁止建 issue）→ 本次首次提交。
- **Fix 建议**: 校验段加 `mapping[i] < output.classes.size()` 上界检查。

### F2-05 · P1 · io:reproject srcCrsOverride 声明了但没用（R0 F-OPS-4 复验仍成立）

- **Location**: `src/operators/io/io_operators.cpp:306-320`
- **Quoted**: `if ( !meta.crs.valid && params::getString( params, "srcCrsOverride" ).empty() )`
  （仅准入检查）→ `warpRaster( input, ..., options, &progress )`
  （options 无 srcCrs 字段）
- **Impact**: CRS-less 输入即使用户声明了 srcCrsOverride 也无法真正参与 warp——
  参数是死参数；"the only sanctioned fallback"（schema 描述）名存实亡。
  同文件 io:clip 对同名参数有功能性消费，证明属漂移非设计。
- **Dedup**: 同 F2-04（R0 未提交）。
- **Fix 建议**: 把 override 值灌入 warp 路径，或 schema 改为"仅准入"并更新描述。

### F2-06 · P2 · GF/HJ band-role 波长 note 与公开规格偏差

- **Location**: `data/products/band_roles/gaofen.json:9-12`（gf1_pms B1/B3）、同文件
  gf2_pms/wfv 段、`data/products/band_roles/hj.json` B1/B3/B4
- **Quoted**: `{ "band": "B1", "role": "blue", "wavelength_nm": 470.0, "note": "0.42-0.52 um multispectral" }`
- **Impact**: 公开规格 GF-1 PMS B1 0.45–0.52（中点 485）、B3 0.63–0.69（中点 660）；
  本表 0.42–0.52/0.61–0.69（470/650），HJ 同类。wavelength 经 asset 元数据写入科学产物；
  `tests/test_cn_products.cpp:303` 把偏差值断言为黄金（偏差被测试固化）。
- **Dedup**: 基线前无该文件（#956 新建）。
- **Fix 建议**: 按 CRESDA SRF 逐卫星核对 note + wavelength_nm，并更新黄金测试值。

### F2-07 · P2 · cn import 无 sidecar 时假设 TIFF 波段序 == 表序

- **Location**: `src/operators/rs/rs_cn_import_operator.h:123`、`:153`
- **Quoted**: `for ( const sicnu::geo::CnBandSpec &spec : product.bandTable.bands )` /
  `band.sourceBand = i + 1;`
- **Impact**: `bandSource=band_role_table` 只报告不校验；TIFF 物理波段序与文档表序
  不一致时产出角色错标的"分析就绪"数据（NDVI 红绿反转类事故）。
- **Fix 建议**: 无 sidecar 路径加波长一致性校验或显式降级标记。

### F2-08 · P2 · sun_elevation 派生溯源未落地输出面

- **Location**: `src/geospatial/products/cn_product_metadata.cpp:625`、`:636`
- **Quoted**: `sunElevationSource = "derived as 90 - declared solar zenith";` /
  `if ( !sunElevationSource.empty() && product.extra.size() < 16 )`
- **Impact**: 全仓 `sun_elevation_source` 仅此一处；`cnImportResult` 不透出；extra 满
  16 项时溯源静默丢弃而派生值照写——消费者无法区分实测与派生高度角。
- **Fix 建议**: 溯源字段进 `cnImportResult`；extra 满时显式报错而非静默丢。

### F2-09 · P2 · spectral Gaussian 重采样跳过源网格单调校验

- **Location**: `src/processing/algorithms/spectral_resampling.cpp:89`
  （对比 `:17` 线性路径的 `srcWl[i] <= srcWl[i-1]` 校验）
- **Quoted**: `if ( !std::isfinite( targetWl ) || targetWl < srcWl[0] || targetWl > srcWl[srcBands - 1] )`
- **Impact**: 头文件契约（spectral_library.h:216-219）要求两侧严格递增；Gaussian 路径
  对非单调网格直接产出无意义加权平均。`matchSpectrum` 波长变体
  （spectral_library.cpp:873）只查尺寸不查单调，把未验证网格喂给该内核。
- **Fix 建议**: Gaussian 路径补同款单调校验；matchSpectrum 对非单调 entry 跳过。

### F2-10 · P2 · lab copilot teacher 身份仅 schema 层不声明、无强制剥离

- **Location**: `src/agent/harness/lab_tools.cpp:82-85`（注释）、
  `src/agent/harness/lab_copilot.cpp:369`（`labAsk` 直读 `input["role"]`）
- **Quoted**: `// Deliberately NOT in the schema: "role" and "teacher_token". They are` /
  `// host-injected session credentials (V1 hardening)`
- **Impact**: 模型在 `lab_ask` 入参塞 `role:"teacher"` 即被按教师不加闸解析；
  今日无实际泄露（lab 动作面只读）属行为巧合非设计保证。
- **Fix 建议**: 工具分发层对 lab 域入参加 role 白名单剥离；加"伪造 teacher 被拒"测试。

### F2-11 · P3 · ADR 编号 0146 被 9 个文件复用

- **Location**: `docs/adr/0146-*.md`（9 件：capability-relation-graph、cn-product-adapters、
  lab-auto-grading、lab-copilot-teaching-constraint、lab-report-schema、labspec、
  offline-degradation-contract、spectral-library-material-priors、unified-rs-terminology-contract）
- **Impact**: "ADR 0146" 无法唯一定位；R1 已记录 ADR 0144 三重复用（D-027），0146 九重复用
  更严重；并行 track 各自从 0146 起编号说明编号分配无仲裁。
- **Fix 建议**: domain-modeling 体系补编号仲裁规则；存量 9 件重编号。

### F2-12 · P3 · .planning 白名单仅覆盖 17/39 个 track（含本 track）

- **Location**: `.gitignore:120-145`（白名单块）、缺失例 whole-repo-line-review 等 22 个目录
- **Impact**: R1 缺陷 D-005 的延续：未覆盖 track 的新增 planning 文件仍会被静默吞掉；
  R0 的规划文件能入库纯靠历史先跟踪。
- **Fix 建议**: 白名单补全 22 个历史 track（一次性），或改用 `.planning/*/` 反向规则。

### F2-13 · P2 · 检测 NMS O(n²) 且无取消点（R0 F-OPS-5 复验仍成立）

- **Location**: `src/operators/runtime/detection_postprocess.cpp:130-164`
- **Impact**: maxDetections=100000 上界下最坏 1e10 次 IoU；无取消检查，长任务不可中断。
- **Fix 建议**: 分块/网格索引 NMS；循环内加取消点。

## 撤下记录（复验后不提交）

| 候选 | 撤下理由 |
| --- | --- |
| R0 F-OPS-2 fromMat | HEAD 已修复（按行拷贝） |
| R0 F-OPS-3 qa_mask | HEAD 已修复（显式守卫+计数） |
| R0 F-PI-1 桥失步 | 缓解在位（坏行丢弃+续消费） |
| R0 F-PI-2 deadline | 已双侧落地 |
| B4 calibr 变体命名 | 置信度低且单文件内推测性分支，无第二证据 |
| B5 父目录名劫持 | looksLikeCresdaXml 兜底在位，降级后证据不足以支撑 |
| B7 单波段误路由 | 触发条件需解析缺陷先行，属推测链 |
| B8 loader 测试顺序依赖 | 注释即设计（串行 ctest 下成立）；--order rand 非本仓库用法 |
| C3 front/back 覆盖判断 | 对内置已验证库无影响；未验证库路径与 C2 同源，已被 F2-09 覆盖 |
| C4 FWHM 口径 | sensors.json 自认 Gaussian 近似，属文档张力非缺陷 |
| D4 错误信封绕闸 | 无在-tree 生产者，属未来风险；记入横切观察不提交 |
| D5 空 routed_tool | 影响面限于语义缝隙，证据不足 |

## 横切观察（不提交，留给后续 track）

- `feat/execution-id-module`（唯一未合并分支）把 `.claude/skills/` 镜像改 symlink——与
  R1 SKILL_MIRROR.md 的"副本"机制冲突；Windows `core.symlinks=false` 下退化行为待验证。
- D-027（ADR 0144 三重复用）+ F2-11（0146 九重复用）：ADR 编号仲裁缺失是系统性问题。
- goal-template 的 `git check-ignore` 存在性断言写法过严（否定命中时 exit 0 有输出；
  正确判据是"命中行必须带 `!` 前缀"）——R1 模板元发现，已在本 track runbook 步骤中绕过。
