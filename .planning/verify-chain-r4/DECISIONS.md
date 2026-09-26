# DECISIONS — Track 16 验证链收口（链路规则，定稿）

> 每条规则与代码实际行为一致（WP-G 抽查对照：D2↔WP-D 套件、D1↔locale 矩阵、D3↔WP-C 案、D4↔WP-E/F 案）。

## D1 digest 产出点登记规则

链上每一处"canonical 文本 → sha256/fingerprint"产出点必须登记于 BASELINE.md §7（rg 机械枚举全集 P1-P12 + SC/S 扩展），并满足其一：

- **(a) 写出器规范上 locale 无关**：grader canonicalizeJson 走 std::to_chars（规范保证 + grader_json.cpp:139 头内论证）；preflight 全部 canonical 写出经 jsoncpp 1.9.8 writeString（**本机探针实证**：de_DE.UTF-8 下 0.30000000000000004 → "0.3"、1234.567890123 → "1234.56789012"，小数点不随 LC_NUMERIC 迁移）。
- **(b) 产出点入口单点钉 ClassicNumericLocale**：verify canonicalJsonText（verify_types.cpp:663，uselocale per-thread）；parse 侧 :775、verify_pack :142。
- **(c) 书面豁免**（漂移方向安全）：science_context assetDigestOf —— cache-key 用途，locale 漂移只会分裂键（miss 重算），永不跨 locale 碰撞或陈旧命中；preflight/sha256.h 第三份 SHA-256 —— Qt-free 叶子不跨引用（与 grader 同款取舍），KAT 同源。

纪律：禁止在调用方散落 setlocale。反空洞性：任何 locale 矩阵行必须先证明敌对 locale 真咬到格式化器（snprintf 产 "0,5"），否则该行降级为"已装 locale 扫描"并 WARN。
执行证据：tests/test_verify_chain_locale_matrix.cpp —— 13 产出点 × {C, POSIX, C.utf8, en_US, zh_CN, de_DE(LOCPATH 自供给)}，194 断言，两轮绿。

## D2 失效联动合同（精化版）

preflight 与 science_context 可共享同一权威投影（PassportResolver 同形 + capability 表双消费）。失效接缝（invalidateAsset/invalidateAllAssets/notifyProjectSwitch/setCapabilityFacts/refreshRecipes）之后，**同输请求不得复用失效前事实产物**：

- broker 侧：invalidateAsset/invalidateAllAssets/notifyProjectSwitch/setCapabilityFacts 无条件清 bundle 缓存（含"未相关键"——**全量清是有意语义**，测试钉死，勿"优化"成窄失效）；capability revision 是 cache key 材料，reload 推进即 miss。
- preflight 侧：引擎无内部缓存，每次 evaluate 现场咨询权威；权威状态变了，答案跟着变（typed unknown / 新 verdict），**陈旧 pass 永不可存活**。
- **状态变化判据（WP-D 实测精化）**：`refreshRecipes()` 在 registry 为 null 时早退 false —— 路由器本就空，**权威状态零变化**，同键 bundle 命中是合法缓存而非陈旧（live authorities 套件钉死真 reload 失败路径：非 null registry 重载失败时缓存仍清，因路由器状态确实变了）。"不回退陈旧事实"约束的是**状态变化后**的服务。
- 镜像不可读（entryReadable=false）时，band_role/radiometric/modality/temporal/model_compat 五族规则让渡为规则级 pass，故障 finding 由 **operator_known 单点持有**（rules.cpp:173-175 显式注释 + 契约测试钉死）—— 报告级 fail-closed 由 verdict（requires_ack）保证，finding 不重复播报。

执行证据：tests/test_preflight_authority_invalidation.cpp —— 20 用例 294 断言，decl + 两个随机种子三轮绿。

## D3 两级 outcome 合成规则

provider 未挂载 = Indeterminate(verify:i_provider_missing)；不可判读（非有限数/不可读/词汇外 kind）= Indeterminate；可检出违约 = Fail(verify:e_*)；坏 spec（含空 checks："an empty verification must not exist"）= 单条合成 spec.valid Fail（verify:e_invalid_spec）；params 走私非有限数 → 不可 seal → 同合成 Fail。多检查折叠格：any Fail → Fail；else any Indeterminate → Indeterminate；else Pass；**空输入 → Indeterminate**（81-permutation fuzz 钉死）。同 spec 内矛盾 pin 不仲裁：各判各的，折叠格给整体，双 verdict 均在封印报告中可见（WP-C 案）。

## D4 "不可评"判据（grader/suitability）

- grader：证据不完备（required-evidence 违约 → Blocked+0；必需 metric 缺席 → criterion Indeterminate + grader:metric-missing，绝不静默零分）；"整份不可评"与"诚实零分"在 verdict 层同为 Fail，但在 criterion status（Indeterminate vs NotEarned）+ reasonCodes 上**机器可区分**（WP-E 案钉死对照）。预算超限 = typed 拒绝无报告（既有）。reports carry no wall clock —— **无超时语义属合同事实**，本轨不引入（新增语义=新方向，禁止）。
- suitability：provider 失败 = typed 向上传播（诊断词原样，WP-F 案）；显式 facts 优先，provider 仅在 facts 缺席且 datasetVersionId 指名时被咨询（spy 零咨询案）；-1=unknown、0=测量值（store 案）；factsTruncated 必须降级可见。
- 冲突/过期事实：**合同无此判定面**（Inputs 单 facts 通道 + DatasetFacts 无采集时间戳）→ 不新增判定（铁律），"两事实源"唯一真实形态 = 显式 facts vs provider，其优先级即 D4 的 precedence 合同。

## D5 豁免清单

| 对象 | 豁免理由 | 漂移方向分析 |
|---|---|---|
| science_context assetDigestOf（Json::writeString+fnv1a64） | cache-key 用途，非跨机合同 | locale 漂移 → 字节变 → 键变 → miss 重算；无碰撞/无陈旧命中 |
| preflight/sha256.h 第三份 SHA-256 | Qt-free 叶子不跨引用（grader_sha256.h 同款先例明示） | 实现同源 KAT；行为一致由各方 KAT 独立保证 |
| metric seam 无生产 IMetricView | 生产事实源在 workflow/lab metrics 域（他轨） | 判据由 fakes 全覆盖；补 adapter=新方向 |
| QString::arg(double)（criteria_spectral summary） | 人读 summary 非机器合同；Qt 6.11 实证 locale 无关 | 本机探针 de locale 下 "0.5" 不变 |

## D6 ctest 口径（Oracle 修正）

字面 Oracle `verify|grader|preflight|suitab|science_context` 在 ctest 名上漏 verifier/verification 家族（不含 "verify" 子串）。本轨绑定口径：`verif|grader|preflight|suitab|science_context|evidence`（超集），两轮全绿同时覆盖字面口径。域测试面 = 47 文件（BASELINE §6 钉死口径）。

## D7 非本域发现登记

`cmake/raise-compiler-stack.sh`：`status=$?` 在整个 if 语句后捕获，POSIX sh 条件失败无 else 时 if 退出码为 0 → 确定性编译错误重试 12 次后 `exit 0`（假绿）。**build-infra 轨所有，白名单外不修**；本轨以 gcc-15 配置绕过（launcher 仅 GCC≥16 挂载）。已在 PR 正文披露。
