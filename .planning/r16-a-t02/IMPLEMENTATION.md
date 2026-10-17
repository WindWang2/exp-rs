# R16-A-T02 IMPLEMENTATION — src/grader + src/faultlab 判定与故障演练轨道

分支 `hardening/r16-a-t02-grader-faultlab`,基线 46791bc65。三个代码 commit:

| Commit | WP | 内容 |
|---|---|---|
| ba5ebd0af | WP-A | faultlab canonical/digest fail-closed 对齐 |
| 516087075 | WP-C | 场景加载器操作数诚实性 + 可复现/无残留回归 |
| 295f507b2 | WP-D | grader canonical 递归深度预算 |

## WP-A — faultlab canonical/digest fail-closed 对齐(ba5ebd0af)

T01 移交的三个分叉点,全部先实证再修:

1. **非有限数**。实证:jsoncpp(1.9.x)把 `1e999` 解析成 +Infinity 且**无解析错误**
   (probe1);期望操作数经 `isNumeric()/asDouble()` 一路进入 `ExpectationResult.expected`
   → `reportDigest` 里出现伪 JSON 字面量 `Infinity`。修法(两层):
   - 摘要层(`util/canonical_json.cpp` `appendDouble`):±Infinity → 类型化拒绝
     (与 verification canonical 的 "an unhashable value must not reach a digest" 同款);
     **NaN 保留 `NaN` 拼写** —— 它是 fault_types.h 文档化的 no-data 标记,网格摘要依赖
     它(既有测试 "canonical grid digest is stable and NaN-safe" 钉住成功摘要),符号位
     归一(±nan 同拼写)。这是"注入语义(NaN=功能)与摘要语义(Inf=不可哈希)"的区分。
   - 加载器门(WP-C commit):期望操作数 finite 检查,非有限 → `faultlab.scenario_field`。
2. **负零**。实证:`%.17g` 对 -0.0 输出 `-0`(verification T01 已归一 "0",grader
   canonicalNumber 注释明确归一 "0")。修:`value == 0.0 → "0"`,两种零同摘要。
   实证无契约破坏:7 个内置 fixture 的 canonical 文本中 `-0` token 计数为 0(probe4),
   golden 摘要 `two_band_index_pair@42` 改前后一致(测试钉死)。
3. **无界递归**。实证:O(n) 构造 10 万层深 Json::Value → `sha256HexOf` SIGSEGV
   (probe3,exit 139)。修:walk 带 depth(256)/node(10M)/output(256MiB)三重预算,
   深度上限取场景 reader stackLimit=128 的两倍 —— 凡能 parse 的文档必可摘要;
   上限仍远低于栈危险区。

**错误通道**(既定约束:值语义 API 被不可改签名的既有测试使用):
- 新增 `tryToCanonicalString`/`trySha256HexOf`(bool + out + error,镜像 verification
  canonicalJson 的形状;无第二摘要器、未链 sicnu_verification);
- 原值返回形式失败时返回**空串**(grader 的 no-digest 先例;合法值不可能序列化成 0 字节);
- `fault_runner`:摘要失败 → 新诊断码 `faultlab.digest_failed`(append-only 词汇表),
  `sourceUnchanged`/`replayDeterministic` 绝不对空摘要声称 true(空==空 会假真),
  `passed=false`;`faultReportToJson/faultReportDigest` 拒绝时空串。
- 残留暴露:jsoncpp `~Value()` 自身按层递归析构(上游属性,本仓所有 jsoncpp 消费方
  共有),故测试用 2000 层而非 20 万层演示类型化拒绝;10 万层崩溃证据在探针记录。

## WP-C — 加载器诚实性 + 可复现(516087075)

1. **PRESENT 但错误类型的操作数静默跳过**(#1464 同类 fail-open):`{"relation":
   "delta_ge","value":"0.5"}` 曾静默按 value=0.0 判定(delta≥0 空洞通过)。修:
   present 的 value/range_lo/range_hi/text 一律类型检查 + 数值 finite 检查;
   absent 保持 0.0 legacy(changed 不携带操作数)。
2. **base_fixture.params 非空静默忽略**:makeFixture "v1 fixtures take no parameters"
   且完全忽略 params —— 作者声明 width=64 静默得到 16×16。修:非空 → 类型化拒绝;
   空对象保持合法(全部现存生产者都传空)。**集成注意**:data/faultlab/
   faults.schema.json 仍把 params 描述为自由对象(本轨道不可写 data/),描述需同步。
3. **可复现/无残留钉子**(私有 sandbox root,避免并行 flaky):同 seed 3 次运行 →
   reportDigest/replayDigest 全等、每次 sandboxRemoved、root 零残留。
4. 复核既有事实:非法注入不改源(applyFault 只作用于副本;r9 fail-before-mutate 测试
   钉死)、before/after 摘要只在真实源上比较(digest 失败新路径见 WP-A)。

## WP-D — grader canonical 深度预算(295f507b2)

实证:文本路径受 reader 默认 stackLimit=256 保护(probe5:>256 层文本 throw,
parseJsonStrict 捕获为类型化拒绝),但引擎是程序化 API —— evidence.facts 逐字进
toJson() 后到无预算 walk,10 万层 facts 在 grade() 摘要步骤 SIGSEGV(探针)。修:
512 层上限(两倍 parse 上界,合法文档摘要不变),走既有 GraderError 通道;
grade() 端到端得到 SchemaShapeInvalid 类型化拒绝、无报告(新测试钉死)。

## WP-B — #1464 复核与判分语义(无代码改动,全部 FIXED 实证)

- **#1464 grader 侧**:`parseSequencingStamp` 已按时刻(instant)比较,6 个专门测试
  钉死(test_grader_engine.cpp 296-488 行:小数拼写/时区偏移/不可解析/相等时刻/
  分钟小数拒绝/闰秒与小写 t guard),基线与本轮全绿。**不重造代码**。
- **#1464 faultlab 侧**:cell_size 已消费(test_faultlab_params_r8);本轨道补齐同类
  (fixture params、期望操作数,见 WP-C)。
- **缺证据绝不当通过**:all-indeterminate → verdict Fail 且 reason 链与诚实零分可区分
  ("an all-indeterminate report stays machine-distinguishable");required-evidence
  缺失 → Blocked 0 分;部分提交 → Partial 且未覆盖项 Indeterminate+0 分。全部有既有
  测试钉死,基线全绿。
- **ISO-8601 语法分歧(R14-E 残留)**:grader 拒 date-only/>9 位小数/小写 t(→
  indeterminate,fail-closed in-domain);resolver(scientific_state,本轨道不可写)收
  date-only/任意位小数。两侧在各自域内 fail-closed,不产生字节序判决;统一需动
  域外代码,记录为跨轨道残留,不修。

## WP-E — 性能(有实测,无优化必要)

见 PERFORMANCE.md:1e4 条(hard cap)grade() ~380ms / 37.6MiB,摘要字节稳定;
判定为无真问题(O(n·m) 不存在:8 criteria × 1e4 items 线性扫描),不优化。

## WP-F — fault_runner/sandbox 审计(结论:无子进程面)

faultlab 是纯内存管线:无超时/取消/子进程终止面可审(故障注入是对内存副本的变换,
sandbox 只是 temp 目录)。temp 目录生命周期:每 run cleanup+verifyNoResidue、析构
兜底(既有测试)+ 本轮新增私有 root 零残留钉子(WP-C)。`/tmp` 扫描无
sicnu-faultlab-* 残留(测试后人工验证)。

## WP-G — render/错误码确定性(结论:已钉死,无改动)

三个 render 是 report 的纯函数("all renderers are deterministic" 钉死);
faultlab 诊断码 `faultlab.*` append-only,本轨道仅新增 `faultlab.digest_failed`;
grader 错误码/理由码词汇表封闭且有 round-trip 测试。未发现新分叉。

## 范围合规

仅写:src/faultlab/**(canonical_json.{h,cpp}、fault_report.cpp、fault_runner.cpp、
fault_scenario.cpp)、src/grader/grader_json.cpp、tests/test_r16_a_faultlab.cpp、
tests/test_r16_a_grader.cpp、tests/CMakeLists.txt(两个注册块)、.planning/r16-a-t02/**。
未触碰 src/verification、src/teaching*、data/、test_canonical_digest_parity.cpp。
构建全部在 -j2 + 共享租约内完成;未 push、未建分支。
