# R16-A-T01 IMPLEMENTATION — src/verification 深度审计、修复与回归

基线 46791bc65(master),分支 hardening/r16-a-t01-verification。
所有改动仅触及 `src/verification/**`、`tests/test_r16_a_verifier.cpp`(新 lane,注册为
`test_verifier_r16a_14`)、`tests/CMakeLists.txt`(lane 循环后 20 行最小注册)、本目录。

## 方法

先读后写:通读 `src/verification` 全部 36 个头/源文件与七个 `_14` lane,并在仓库实际链接的
系统 jsoncpp 1.9.8 上用独立探针程序(/tmp/json_probe.cpp,不进仓库)实证了四个行为假设,
只有实证为"真实存在且可复现"的才修;每个行为改动先写 RED 测试、记录失败输出、再修、再
GREEN×2。

探针实证(jsoncpp 1.9.8,/usr/include/jsoncpp):

| 访问 | 输入 | 结果 |
|---|---|---|
| `asString()` | object/array | 抛 `Json::LogicError: Type is not convertible to string` |
| `asInt64()` | uint64 > INT64_MAX(`isIntegral()`==true) | 抛 `LargestUInt out of Int64 range` |
| `asInt()` | 整数超出 int32(`isIntegral()`==true) | 抛 `LargestInt out of Int range` |
| `ostringstream << -0.0`(classic, precision 12) | 负零 | `"-0"`(而 `0.0` → `"0"`) |

## WP-A #1488 后续核验 + canonical 边界(commit 357bd0ab7)

**动机**:#1509 已修 asInt64 溢出,但七个 lane 只覆盖了 uint64>INT64_MAX、NaN、深度>32、
失败时 out 不动四处;五重预算中字符串/输出/成员/节点四条边、±inf、INT64 两端、2^53 均无
测试触达。

**根因(行为缺陷)**:`roundSignificant(-0.0)` 输出 `"-0"`、`roundSignificant(0.0)` 输出
`"0"`。IEEE 规定 `-0.0 == 0.0`,两次计算同一逻辑值的 run 会得到不同 canonical 文本、不同
evidence 拼写(`canonicalNumber` 直接用 roundSignificant)和不同 digest —— 与模块自己的契约
("同一逻辑值必须字节相同",如 0.1+0.2 vs 0.3)直接冲突。RFC 8785 (JCS) 为同一理由做了同一
归一化。

**改动**:
- `src/verification/canonical_json.cpp` — `roundSignificant` 先把 `== 0.0` 的值归一为 `+0.0`
  (只影响负零的符号位,其余输入逐位不变)。digest/evidence 两条路径一次修复。
- `tests/test_r16_a_verifier.cpp` — A-1(RED→GREEN):roundSignificant/canonicalJson/
  canonicalDigestSha256/canonicalNumber 四个缝的负零一致性;A-2/A-3(纯补覆盖,实现已正确):
  深度 32/33、字符串 4096/4097、成员 10000/10001、节点>50000、输出>4MiB(1100×4000B 合法短
  字符串 ≈4.4MiB)、INT64_MIN/MAX 精确、uint64 两端、±inf 拒绝、2^53 处"整数值 double 走
  isInt64 精确整数拼写"的跨类型一致性质(比 12 位更精确,原测试预期写错后按实际正确行为修正)、
  1e19→uint64 精确、1e300/π→12 位钉死。

**发现修正**:最初 A-3 预期 double 2^53 → "9.00719925474e+15" 是错的:jsoncpp `isInt64()`
对整数值 double 返回 true,canonical 输出精确整数 "9007199254740992"。这是更强的正确性质
(跨类型一值一拼写),按实际行为钉死,无代码改动。

## WP-B jsoncpp 无类型访问清点 #1447 同型(commit 92edf5a86)

**动机**:全模块 grep `asString/asInt/asInt64/asDouble/asBool/isMember/getMemberNames`
逐处核对类型前置检查(完整清单见 TEST_MATRIX"访问点清点"表)。

**真实逃逸三处(全部实证 RED)**:

1. `verification_types.cpp requireCount` + `report.cpp BudgetUsage::fromJson readCount`:
   预算计数成员仅有 `isIntegral()` 前置,`"max_checks": 18446744073709551615`(jsoncpp 存为
   uint64)直达 `asInt64()` 抛 `LargestUInt out of Int64 range`,异常穿出
   `VerificationSpec::fromJson` / `VerificationReport::fromJson`,违背 false+error 契约 ——
   与 #1488 同型、不同成员名。
2. `spec.cpp`/`report.cpp` 的 `schema_version`、`budget.max_depth`、
   `budget_usage.max_depth_seen` 用 `asInt()` 读:仅 `isIntegral()` 前置,超出 int32 的整数抛
   `LargestInt out of Int range`。
3. `checks_artifact.cpp requiredKeySet`:`facts["kind"].asString()` 无类型前置,且该路径
   (未声明 kind 期望、仅 required_keys/size/crs 的检查)无任何屏障兜底 —— `runSpec` 无
   try/catch,artifact 描述里 `kind` 为对象/数组时 `Type is not convertible to string`
   直接穿出 runSpec,整个报告丢失(比误报 PASS 更早的失效:崩溃式 fail-open)。

**修复(最小改动,语义只对原抛异常输入变化)**:
1/2. 用 jsoncpp 自带 `isInt64()`/`isInt()` 做范围前置(bool 保持原可接受语义),错误消息不变;
3. `requiredKeySet` 改为 `optionalMember`+`isString()` 守卫:不可读 kind 与任何可读非 raster
  kind 同待遇(不描述网格 → 不追加 size 必需键);声明了 kind 期望的路径原本已有守卫
  (Indeterminate),不变。修复后:只问 dtype 的检查凭已有证据照常结论(Pass 是诚实的 —— 问题
  声明了什么就评什么),问 kind 的检查保持 Indeterminate,报告整体 Indeterminate,不再抛。

**明确不做的**:不给 `runSpec`/`dispatch` 加全量 try/catch 屏障。providers.h 明文契约是
provider 用 Availability 返回而非抛异常;#1509 给 canonicalJson 加屏障是因为它接收任意
Json::Value。checks_* 的纪律是逐访问点类型守卫(全部七个族已逐处核对), blanket 屏障会掩盖
新的守卫缺失而不是修复它。B-3 修在访问点,与该纪律一致。

## WP-C checks_* 七族 fail-closed 审计(结论,无代码改动)

七族(state/artifact/numeric/relational/provenance/reproducibility/cross_output)逐族核对:
缺失 artifact→Indeterminate(kArtifactMissing)、资产身份漂移(kind/CRS 不匹配→Fail 且带
observed/expected)、无法取证(Missing/Refused 分开编码)、部分结果(半边 cross-output、
单边 relational→Indeterminate)、NaN/inf 先于比较拒绝(Fail kNumericNotFinite 或
Indeterminate,见各族注释)、空期望集→Indeterminate。除 WP-B 第 3 项外未发现反例;
adversarial lane 20 个 G-* 用例与本轮 B-3 补齐后无已知逃逸。

## WP-D digest/报告链路确定性(覆盖,commit 6058c2050)

16 个键嵌套 fixture 按 16 种插入序(载荷按键固定,仅换插入顺序)canonical 文本与 digest 全等;
变更一字段 digest 必变;`runSpecFlat` 重复 4 次 reportDigest 全等;`specDigest` 对 checks 声明
顺序无关(canonicalProjection 排序)。时间戳:模块无任何 clock 调用(grep 无 time/now),
确定性只取决于输入 —— E 项全局 locale 攻击下不变。

## WP-E 跨平台/locale 独立性(覆盖,commit 6058c2050)

自定义 numpunct facet(小数点 `,`、千分位 `.`、三位分组)装为全局 locale,先证明默认构造
流确实输出 "1,5",再断言 roundSignificant/canonicalJson/canonicalNumber/digest 全部保持
classic 拼写("1.5"/"{\"big\":1e+21,\"half\":0.5,\"sum\":0.3}"),恢复全局 locale 后 digest
不变。结论:12 位契约在 hostile locale 下稳定,实现已正确(imbue classic),纯覆盖。

## WP-F 性能预算(实测,不优化)

详见 PERFORMANCE.md:49996 节点 canonical 化+digest,单用例 wall 0.10s、峰值 RSS 16192 KB
(含进程启动与 Catch2);整 lane(10 用例含全部预算炸弹)0.44s / 25356 KB。写法为单遍追加式
O(n) 序列化器,`counted()` 每节点 O(1);未发现 O(n²) 或重复全量复制,不优化。

## WP-G packs_builtin/render 事实字段审计(覆盖+跨轨道,commit 6058c2050)

事实:三个内置 pack 的 params 使用求值器不消费的词汇 —— structural(`expect_present`/
`expect_kind`/`expect_grid` vs artifact_shape 的 `expect.kind/size/crs_authid/required_keys`)、
provenance(`required_fields`/`require_resolved_inputs` vs `required_dimensions`)、
reproducibility(顶层 `algorithm`/`repeats` vs `expected.algorithm+expected.digest`)。后果:
即便 provider 全答 Found,每个内置检查都 Indeterminate(fail-closed,无误报 PASS —— G 测试
钉死这一点),pack 功能上是占位符。这是 R14-E 残留 "spec_runtime authoring gap" 的另一面:
占位参数等待 authoring 层替换,而该层尚未存在。仓内无这些 pack 的运行时消费者
(grep 仅 packs lane),故不在本轨道改名(改词汇会改变 pack 语义与 digest,属 authoring 轨道
决策)。render_agent/render_teaching 事实字段与 failure_codes 表核对一致(派生自同一
derivedStatus/derivedFailureCodes,无双源)。

## WP-H #1509 复查(结论,无代码改动)

`git diff 46791bc65^1 46791bc65 -- src/verification`:对 canonical_json.cpp 的全部改动是
`canonicalJson` 顶层 try/catch(std::exception + catch-all → false+error);序列化语义零漂移
(isInt64/isUInt64 分支在父提交已存在)。结论:修复是屏障+既有分支,无语义漂移。本轮 WP-A/B
在它之上补了屏障盖不住的输入(屏障把异常变成 refusal,但 loader 契约是 false+error、runner
契约是 Indeterminate 结果 —— 本轮把三处逃逸修到契约要求的形态)。

## 旧 Issue 四态归类

- **#1488(canonical_json asInt64 溢出)**:已修复附证据(#1509 修主体;本轮 WP-A 补负零
  digest 分叉这一同族残余 + 四条预算边/±inf/INT64 两端/2^53 覆盖,WP-B 修同型的三处
  loader 逃逸)。
- **#1447(jsoncpp 无类型访问,verification 部分)**:可复现已修 —— 三处逃逸
  (requireCount/readCount uint64 溢出、asInt 超界×4 站点、kind asString)实证 RED 后修复;
  其余全部访问点核对有守卫(见 TEST_MATRIX 清点表)。

## 文件清单

| 文件 | 改动 |
|---|---|
| src/verification/canonical_json.cpp | roundSignificant 负零归一(WP-A) |
| src/verification/verification_types.cpp | requireCount/Budget::fromJson 范围守卫(WP-B) |
| src/verification/spec.cpp | schema_version isInt 守卫(WP-B) |
| src/verification/report.cpp | readCount/max_depth_seen/schema_version 守卫(WP-B) |
| src/verification/checks_artifact.cpp | requiredKeySet kind 类型守卫(WP-B) |
| tests/test_r16_a_verifier.cpp | 新 lane,10 个 TEST_CASE(A×3、B×2、D×2、E、G、F) |
| tests/CMakeLists.txt | lane 循环后注册 test_verifier_r16a_14(20 行) |
| .planning/r16-a-t01/* | 本文件 + TEST_MATRIX + PERFORMANCE + ledger |
