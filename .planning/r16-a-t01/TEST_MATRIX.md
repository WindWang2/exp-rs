# R16-A-T01 TEST MATRIX — 测试↔Oracle 映射与 RED/GREEN 证据

测试命令(全部两遍):

```
export PATH="/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin:$PATH"
cd /home/kevin/project/exp-rs-r16-a-t01/build-dev
QT_QPA_PLATFORM=offscreen ctest -j1 -R "^test_verifier_.*_14::" --output-on-failure
```

- 基线(改动前):100 测试全绿。
- WP-A 后:103/103 ×2(green_wp_a_1.log / green_wp_a_2.log,EXIT=0)。
- WP-B 后:105/105 ×2(green_wp_b_1.log / green_wp_b_2.log,EXIT=0)。
- WP-C/D/E/G/F 后(最终):110/110 ×2 —— `100% tests passed, 0 tests failed out of 110`
  (green_wp_defg_1.log / green_wp_defg_2.log,EXIT1=0 / EXIT2=0)。
- 新增 lane:`test_verifier_r16a_14`(源 tests/test_r16_a_verifier.cpp,10 个 TEST_CASE)。

## 测试 ↔ Oracle 映射

| 测试(ctest 名) | WP | Oracle 断言 | 性质 |
|---|---|---|---|
| r16a A-1: negative zero …one digest | A | -0.0 与 0.0 的 roundSignificant/canonicalJson/canonicalDigestSha256/canonicalNumber 全等;delta 路径 digest 不分叉 | 行为修复(RED→GREEN) |
| r16a A-2: canonical budgets trip exactly | A | 深度 32 过/33 拒;字符串 4096 过/4097 拒;成员 10000 过/10001 拒;节点>50000 拒;输出>4MiB 拒;拒绝时 out 不动 | 纯补覆盖(已正确) |
| r16a A-3: integer extremes…12 digits | A | INT64_MIN/MAX、uint64>INT64_MAX 精确;整数值 double(int64/uint64 范围内)与整数拼写一致;1e300/π 12 位钉死;±inf 拒绝 | 纯补覆盖(已正确;原预期笔误按实际修正) |
| r16a B-1: loaders refuse out-of-range integers | B | 预算计数 uint64 溢出、max_depth/schema_version/max_depth_seen 超界、checks_evaluated 溢出 → fromJson false+error 不抛;合法值照常加载 | 行为修复(RED→GREEN) |
| r16a B-3: non-string artifact kind…never a crash | B | kind 为对象/数组时 runSpecFlat 不抛:只问 dtype 的检查 Pass(有证据),问 kind 的检查 Indeterminate,报告整体 Indeterminate,reportDigest 存在 | 行为修复(RED→GREEN) |
| r16a D: key order never changes a digest | D | 16 种插入序 → 同 canonical 文本同 digest;排序字节黄金;改一字段 digest 变 | 纯补覆盖 |
| r16a D: repeated runs…one report digest | D | runSpecFlat×4 同 reportDigest;specDigest 与 checks 声明顺序无关 | 纯补覆盖 |
| r16a E: canonical text ignores a global locale | E | comma-decimal 全局 locale(先证明生效)下 roundSignificant/canonicalJson/canonicalNumber/digest 全 classic;恢复后 digest 不变 | 纯补覆盖 |
| r16a G: builtin packs cannot silently read as Pass | G | 全答 Found 的 provider 下,三个内置 pack 每检查 Indeterminate、报告 Indeterminate;派生 contract pack 正常结论(证明非全局失能) | 纯补覆盖(钉死 authoring gap 的 fail-closed 侧) |
| r16a F: document just under the node budget | F | 49996 节点完整 canonical 化(输出 >250000 字节)+ digest 存在;+5 节点(每子数组 1 个,成员数全合法)恰在 50001 拒于 node budget | 纯补覆盖 + 实测样本 |

## RED 证据摘要(修复前真实输出行)

WP-A(/tmp/red_wp_a.log,ctest EXIT=8):

```
tests/test_r16_a_verifier.cpp:86: FAILED:
  REQUIRE( roundSignificant( -0.0 ) == "0" )
with expansion:
  "-0" == "0"
```

WP-B(/tmp/red_wp_b.log,ctest EXIT=8):

```
tests/test_r16_a_verifier.cpp:268: FAILED:
  REQUIRE( !VerificationSpec::fromJson( spec, specLoaded, error ) )
due to unexpected exception with message:
  "LargestUInt out of Int64 range"

tests/test_r16_a_verifier.cpp:360: FAILED:   (runSpecFlat 整用例)
due to unexpected exception with message:
  "Type is not convertible to string"
```

覆盖测试两次笔误的失败(测试缺陷,非模块缺陷,已按实际行为修正测试):
- D 初版把载荷绑定到插入位置(旋转改变了键→值映射,canonical 文本理应不同);
- F 初版把 5 个额外节点全加进同一子数组(9998+5=10003 先触发成员预算 10000,错误串是
  "member" 非 "node")。

## GREEN×2 记录

| 阶段 | 命令 | 结果 |
|---|---|---|
| WP-A | ctest -R "^test_verifier_.*_14::" ×2 | 103/103,103/103,两次 EXIT=0 |
| WP-B | 同上 ×2 | 105/105,105/105(grep -c Passed=105),EXIT=0 |
| 最终 | 同上 ×2 | `100% tests passed, 0 tests failed out of 110` 两次 |

## 访问点清点(WP-B 全量核对表)

grep `asString|asInt|asInt64|asUInt64|asDouble|asBool` 于 src/verification,逐处:

| 位置 | 访问 | 前置 | 结论 |
|---|---|---|---|
| canonical_json.cpp:107/112 | asInt64/asUInt64 | isInt64()/isUInt64() 分支(#1509) | 安全 |
| canonical_json.cpp:116 | asDouble | realValue 落入 + isfinite | 安全 |
| canonical_json.cpp:144/151 | asBool/asString | booleanValue/stringValue case 标签 | 安全 |
| checks_artifact.cpp:114/174 等 | asDouble | isNumeric 前置 | 安全 |
| checks_artifact.cpp:238(修后) | kind asString | optionalMember+isString(本轮修) | 已修 |
| checks_artifact.cpp:494 | asString | 自建字符串数组 missing[] | 安全 |
| checks_artifact.cpp:564/580 | crs asString | isString 前置 | 安全 |
| checks_cross_output.cpp:63-89/183 | asString/asDouble | isString/isNumeric 前置 | 安全 |
| checks_numeric readOrNull/tolerance | asDouble | isNumeric 前置 | 安全 |
| checks_relational.cpp:76/107 | asString/asDouble | isString 前置 / walkFactPath isNumeric | 安全 |
| checks_reproducibility readPin | asString | isString 前置 | 安全 |
| checks_state/checks_provenance | asString | isString 前置 | 安全 |
| evidence.cpp:76-94 | asString | isString 前置 | 安全 |
| pack.cpp:86-124 | asString | isString 前置 | 安全 |
| render_teaching.cpp:33/60/161 | asString | isString 前置 | 安全 |
| report.cpp:92/125-209/430-462 | asString/asInt | isString 前置 / readStringArray | 安全 |
| report.cpp:271/288/293/448(修后) | asInt64/asInt | isInt64()/isInt() 范围前置(本轮修) | 已修 |
| report.cpp:680/683 | asInt | 本函数自建整数的 +1 | 安全(自建值) |
| spec.cpp:101/106(修后)+118-182 | asInt/asString | isInt()(本轮修)/isString 前置 | 已修 |
| spec.cpp:267-279 canonicalProjection | get().asString | 输入来自 VerificationCheck::toJson()(恒字符串) | 安全 |
| verification_types.cpp:85(修后)/178(修后) | asInt64/asInt | isInt64()/isInt()(本轮修) | 已修 |
| verification_types.cpp requireString/requireObject/requireCount 调用点 | — | require* 内前置 | 安全 |
