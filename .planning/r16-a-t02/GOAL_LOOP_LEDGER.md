# R16-A-T02 GOAL_LOOP_LEDGER — src/grader + src/faultlab 判定与故障演练轨道

宿主适配:ZCode Agent 子代理(等价 /teamwork-preview 执行单元)+ 内嵌五阶段 Oracle 协议(等价 /goal-loop)。

## 环境事实(已由集成 Agent 验证)

- Worktree:/home/kevin/project/exp-rs-r16-a-t02,分支 hardening/r16-a-t02-grader-faultlab,基线 46791bc65。
- build-dev 已 configure 成功(init cache:r16a-init-cache.cmake)。
- 构建命令(租约内、-j2):
  `flock -w 900 /home/kevin/project/exp-rs-build.lock bash -c 'CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build /home/kevin/project/exp-rs-r16-a-t02/build-dev --parallel 2 --target <T>'`
- 测试命令:`export PATH="/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin:$PATH"; cd build-dev && QT_QPA_PLATFORM=offscreen ctest -j1 -R "^(test_faultlab|test_grader)" --output-on-failure`
- 注意:管道接 tail/head 会吃掉退出码;判断成败用 `echo EXIT=$?`。

## Oracle 判定记录(每 WP 一行,RED 证据先于修复)

四态:FIXED(本轨修复)/ VERIFIED(他轨已修,本轨实证,未造重复代码)/
RESIDUAL(记录在案的域外/上游残留)/ NOT-A-BUG(实证为设计内行为)。

| WP | Oracle | 判定 | 证据 |
|---|---|---|---|
| WP-A canonical fail-closed 对齐 | ±Inf 进摘要 / -0 拼写 / 无界递归,三项均先复现 | FIXED | commit ba5ebd0af。RED:toCanonicalString({"neg":-0.0})=="{"neg":-0,"pos":0}";sha256HexOf(±inf) 非空;probe3 exit 139(SIGSEGV@10 万层);jsoncpp "1e999"→+Inf 无解析错误(probe1)。GREEN×2:106/106(含 10 条新 r16a 断言)。NaN 保留 no-data 拼写(既有 "NaN-safe" 测试钉住),golden fixture 摘要 3e89e850… 改前后一致 |
| WP-B #1464 复核 + 判分语义 | recordedAtUtc 按时刻比较;缺证据不当通过 | VERIFIED(两半) | grader 侧:parseSequencingStamp instant 比较 + 6 个专门测试(test_grader_engine.cpp L296-488)基线全绿,未写重复代码。faultlab 侧:cell_size r8 已修;本轨补齐同类(PRESENT-错型操作数静默跳过、非空 fixture.params 静默忽略)→ FIXED 于 commit 516087075。缺证据语义:all-indeterminate→Fail 且 reason 链区分诚实零分、required 缺失→Blocked、部分提交→Partial(既有测试,VERIFIED) |
| WP-C 可复现 + 非法注入不改源 | 同 seed 同摘要;拒绝不改状态;temp 生命周期 | FIXED(钉子)+ VERIFIED(既有) | commit 516087075:私有 root 3 次运行 reportDigest/replayDigest 全等 + 零残留(新测试);fail-before-mutate 与 source-immutable 由既有 r9/runner 测试覆盖(VERIFIED) |
| WP-D grader jsoncpp 类型安全 + 深度预算 | 程序化深值逃逸 | FIXED | commit 295f507b2。RED:canonicalizeJson(2000 层) 返回值(无拒绝);10 万层 facts 在 grade() SIGSEGV(探针)。GREEN:512 层预算走既有 GraderError 通道,parse-legal(≤256)摘要不变,2 条新测试 |
| WP-E 1e4 证据匹配性能 | 实测 before(修复后即基线) | NOT-A-BUG | 1e4(hard cap)grade() 380-397ms / 37.6MiB,摘要 5/5 字节稳定;无 O(n×m);不优化。见 PERFORMANCE.md |
| WP-F fault_runner/sandbox 执行审计 | 超时/取消/子进程/temp 生命周期 | VERIFIED + NOT-A-BUG | 无子进程面(纯内存管线)→ 无超时/取消可审;temp 生命周期由 cleanup+verifyNoResidue+析构兜底(既有测试)+ 本轨私有 root 零残留钉子;测试后 /tmp 无 sicnu-faultlab-* 残留 |
| WP-G render/错误码确定性 | 错误码映射封闭、判定确定 | VERIFIED | 三 render 为 report 纯函数(既有确定性测试);faultlab.* / grader:* 词汇表 append-only,本轨仅新增 faultlab.digest_failed;grader 封闭词汇有 round-trip 测试 |

## 旧 Issue 四态

| Issue | 判定 | 证据 |
|---|---|---|
| #1464(recordedAtUtc 字典序 + 可选参数静默忽略) | VERIFIED(master 已修,r14-E 实证属实)+ 本轨补齐同类 | grader:SequencingStamp 按时刻比较,6 测试钉死(L296-488),全绿,未重造;faultlab:cell_size r8 已修;本轨新关两类同型(PRESENT-错型期望操作数、非空 base_fixture.params),commit 516087075 |
| #1448(grader/faultlab 同型 fail-open) | VERIFIED(master 已修)+ 本轨加固 | grader:缺证据→Indeterminate+0 分,verdict 不可凭缺失通过("all-indeterminate report…" 测试);faultlab:vacuous-pass 已在 r14-E 关闭("Changed without a clean baseline fails" 测试全绿);本轨再加摘要层 fail-closed(WP-A)与加载器操作数门(WP-C) |
| #1447(grader/faultlab jsoncpp 类型安全,部分) | VERIFIED + 本轨扩展 | grader:boundedIntMember/finiteNumberMember/stringMember 全面前置检查(r13-k);faultlab:transforms 全参数类型门(r12 测试);本轨补齐 faultlab 加载器操作数类型门 + 双侧 canonical 深度预算(WP-A/WP-D);canonicalNumber -0/非有限拒绝既有测试全绿 |

## 跨轨道移交(供集成 Agent)

1. **digest 契约变化(T01 需知)**:faultlab canonical 现在 ±Infinity 拒绝(空串)、
   -0.0→"0"、深度>256 拒绝。内置 fixture 摘要零变化(golden 钉死);若 T01 侧
   verification↔faultlab 摘要有对照表需同步,分叉点已收敛为:NaN=faultlab 特有
   (no-data 功能,verification 侧本就拒绝 NaN,两侧不会对含 NaN 文档互比摘要)。
2. **data/faultlab/faults.schema.json**(本轨不可写):base_fixture.params 描述仍为
   自由 object,加载器现已拒绝非空 —— 请同步描述文本。
3. **ISO-8601 语法分歧(R14-E 残留)**:grader↔scientific_state resolver 均域内
   fail-closed(date-only/小数位数接受面不同),不产生字节序判决;统一需动域外代码,保留残留。
4. **jsoncpp ~Value() 递归析构**:上游属性,极深 Json::Value 在析构时仍可栈溢出
   (与消费方无关);本轨摘要层已类型化拒绝,构造极深值本身不在任何生产路径。

## 已知起点线索(须重新实证,不得盲修)

1. #1464(recordedAtUtc 字典序比较、可选 scenario 参数静默忽略):r14-E 实证已修复
   (grader_matcher / fault_scenario)。T02 复核后写"已修复"证据,禁止制造重复代码。
2. src/faultlab/util/canonical_json.cpp 与 src/verification/canonical_json.cpp 的语义分歧
   (T01 轨道记录的跨轨道事项,归本轨道处理):
   - 非有限数:verification 类型化拒绝;faultlab 写 "NaN"/"Infinity" 进 digest
     (fault_report/fault_runner 的 sourceDigest/replayDigest 使用)。
   - 预算:verification 有 depth/members/elements/string/output 五重预算;faultlab 无任何
     递归/大小预算(jsoncpp reader 有自身栈限,但程序化构造的 Json::Value 可绕过)。
   处理纪律:先查现有测试是否钉住 "NaN" 拼写(改语义=改 digest=契约变化,必须谨慎);
   与 verification 对齐时用最小改动+合同测试;不得引入第二摘要器。
3. faultlab 注入 NaN 是功能本体 —— 观测/摘要层对 NaN 的处理必须与"注入"语义区分清楚。
4. grader 是 ADR 0174 叶子库(仅链 jsoncpp);任何修复不得破坏 layer guard。

## 停机规则

- 连续 3 次尝试无进展 → 换假设/缩小范围;8 次 → 重验前提并报告集成 Agent。
- 每个改动必须有先 RED 后 GREEN×2 的回归;不虚构提升数据。
