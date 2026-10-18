# R16-A-T02 TEST_MATRIX — RED 证据 + GREEN×2

命令壳:`export PATH="/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin:$PATH"; cd build-dev &&
QT_QPA_PLATFORM=offscreen ctest -j1 -R <pattern> --output-on-failure`(管道会吃退出码,判定用 `echo EXIT=$?`)。

## 基线(修复前,46791bc65 工作树)

- 指定正则 `^(test_faultlab|test_faultlab_params_r8|test_grader_schema|test_grader_engine|
  test_grader_adapters|test_grader_render)::` → **49/49 通过,EXIT=0**(/tmp/t02_baseline.txt)。
  注:该正则实际只选中带前缀的 grader 测试(14+17+12+6);faultlab 两个目标的 ctest 名
  无前缀("fault lab: …" 43 个 / params_r8 5 个裸名),真实回归面是
  `-R "^fault lab:|^train_test_spatial_leakage|^wrong_scale_offset|^integral params|^every param"`
  = **48 项**(评审第二轮更正:初稿"47 项"与"params 4 个"计数不准)。本轨道轮内以
  全表面(106,构成见下)与集成轮 223 精确名双绿收尾。

## RED(修复前真实失败输出,关键行)

新目标 `test_r16_a_faultlab` 首轮(0/5 通过,EXIT=8,/tmp/t02_red1.txt):

```
test_r16_a_faultlab.cpp:102: FAILED:
  CHECK( canonical::toCanonicalString( doc ) == "{\"neg\":0,\"pos\":0}" )
with expansion:
  "{"neg":-0,"pos":0}" == "{"neg":0,"pos":0}"
test_r16_a_faultlab.cpp:109: FAILED:
  "e85e589142194db46bfb208f04dfe281c4b66ee6d5ad66e5bf48e9c134379880"
  ==
  "ec4f95abcb4e2e3dbe856c3eb2f81995eacb3823d40aa2e78ed4c5e1798f664d"   // -0.0 与 0.0 摘要不同
test_r16_a_faultlab.cpp:135: FAILED: CHECK( canonical::sha256HexOf( inf ).empty() )  // +Inf 进摘要
test_r16_a_faultlab.cpp:138: FAILED: CHECK( canonical::sha256HexOf( negInf ).empty() )
test_r16_a_faultlab.cpp:139: FAILED: CHECK( canonical::toCanonicalString( inf ).empty() )
test_r16_a_faultlab.cpp:158: FAILED: CHECK_FALSE( result.ok )  with expansion: !true   // 1e999→Inf 操作数被接受
test_r16_a_faultlab.cpp:167+: 同型失败(-Inf / range_lo / range_hi / 字符串 value / bool value /
                                  字符串 range_lo / 数字 text / 非空 fixture params)
0% tests passed, 5 tests failed out of 5
```

栈溢出(不能用提交内测试表达,独立探针,g++ 直连 libsicnu_faultlab.a):

```
$ /tmp/t02probe/probe3 100000     # O(n) 构造 10 万层深 Json::Value → sha256HexOf
EXIT_100K=139                       # SIGSEGV:appendValue 无深度预算
canonical(-0.0/+0.0) = {"neg":-0,"pos":0}     # probe2
canonical(nonfinite) = {"inf":Infinity,"nan":NaN,"ninf":-Infinity}
```

jsoncpp 行为探针(probe1,与本仓链接同一系统库):

```
text={"v": 1e999} ok=1 errs= isDouble=1 finite=0 value-print=inf    # 解析无错误,值为 +Inf
```

grader RED(修复前,test_r16_a_grader 0/2,EXIT=8,/tmp/t02_red_grader.txt):

```
test_r16_a_grader.cpp:87: FAILED: CHECK_FALSE( canonical.has_value() )  with expansion: !true
test_r16_a_grader.cpp:88: FAILED: 0 == 2    (无 GraderErrorCode::SchemaShapeInvalid)
10 万层 evidence.facts → grade() 摘要步骤 SIGSEGV(探针;提交内测试用 2000 层断言类型化拒绝)
```

## GREEN ×2(修复后)

全表面正则 `fault lab|params|^(…全部目标含 r16a)::`、排除 `_NOT_BUILT`
(4 个与本轨道无关的未构建占位目标,基线即失败):

```
RUN1 EXIT=0   100% tests passed, 0 tests failed out of 106   (/tmp/t02_final_run1.txt)
RUN2 EXIT=0   100% tests passed, 0 tests failed out of 106   (/tmp/t02_final_run2.txt)
```

106 的构成:49 个 grader 基线(未回归)+ 43 个 "fault lab:"(全部来自 test_faultlab;
初稿"35 + test_fault_matrix 8"分解失实——test_fault_matrix 是另一目标且未在本轮
面内,评审第二轮更正)+ 5 个 params_r8(裸名)+ 10 个 r16a faultlab 新增(此后评审
第一/二轮又各 +1/+0,见集成 TEST_RESULTS.md)。初稿"4 个 params_r8"计数失实,
实为 5。

指定正则(含新目标)单独计数:`^(test_faultlab|test_faultlab_params_r8|test_grader_*|
test_r16_a_faultlab|test_r16_a_grader)::` = **61 项**(49 基线 + 12 新增)。

## 新增测试清单(tests/test_r16_a_faultlab.cpp,10 案例)

| 案例 | 钉住的契约 |
|---|---|
| negative zero canonicalizes onto '0' | -0.0→"0";±0 同摘要 |
| NaN keeps the no-data spelling, ±Infinity refused | `{"v":NaN}` 精确拼写(±nan 归一);±Inf 空串拒绝 |
| non-finite expectation operands refused typed | 1e999→Inf 的 value/range_lo/range_hi → faultlab.scenario_field |
| present-but-mistyped operands refused | 字符串/布尔 value、字符串 range_lo、数字 text → 类型化拒绝;absent 保 legacy |
| non-empty base_fixture.params refused | #1464 同类(fixture 层);空对象合法 |
| deep Json::Value → typed refusal | 200 层合法摘要;2000 层 "depth budget" 拒绝,值形式空串 |
| try* forms carry the refusal reason | 成功/失败两态;out 失败时不触碰 |
| builtin fixture digests unchanged | golden 3e89e850…(改前后一致)+ 全 fixture 摘要非空(NaN 网格含) |
| report with unhashable number gets no digest | faultReportToJson/Digest 空串;有限孪生 64 位摘要 |
| same seed replays + no sandbox residue | 3 次 reportDigest/replayDigest 全等;私有 root 零残留 |

## 新增测试清单(tests/test_r16_a_grader.cpp,2 案例)

| 案例 | 钉住的契约 |
|---|---|
| canonicalizeJson refuses pathologically deep values typed | 256 层合法;2000 层 SchemaShapeInvalid+"depth" |
| deep evidence facts → typed refusal in grade() | 引擎端到端类型化拒绝无报告;合法孪生正常 Pass |
