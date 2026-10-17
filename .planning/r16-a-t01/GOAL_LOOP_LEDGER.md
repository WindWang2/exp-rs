# R16-A-T01 GOAL_LOOP_LEDGER — src/verification 证据核验轨道

宿主适配:ZCode Agent 子代理(等价 /teamwork-preview 执行单元)+ 内嵌五阶段 Oracle 协议(等价 /goal-loop)。

## 环境事实(已由集成 Agent 验证)

- Worktree:/home/kevin/project/exp-rs-r16-a-t01,分支 hardening/r16-a-t01-verification,基线 46791bc65。
- build-dev 已 configure 成功(init cache:r16a-init-cache.cmake,见集成树 .planning/r16-a/)。
- 构建命令(租约内、-j2):
  `flock -w 900 /home/kevin/project/exp-rs-build.lock bash -c 'CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build /home/kevin/project/exp-rs-r16-a-t01/build-dev --parallel 2 --target <T>'`
- 测试命令:`export PATH="/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin:$PATH"; cd build-dev && QT_QPA_PLATFORM=offscreen ctest -j1 -R "^test_verifier_" --output-on-failure`
- 注意:管道接 tail/head 会吃掉退出码;判断成败用 `echo EXIT=$?`。

## Oracle 判定记录(每 WP 一行,RED 证据先于修复)

四态:已修复附证据 / 未复现 / 可复现已修 / 放弃+理由。

| WP | Oracle | 判定 | 证据 |
|---|---|---|---|
| WP-A #1488 后续核验 | 七 lane 缺:字符串>4096、输出>4MiB、成员/节点预算边、±inf、INT64 两端、2^53;另发现真实缺陷:roundSignificant(-0.0)="-0" 使 IEEE 相等的两值 digest 分叉 | 可复现已修(负零归一,commit 357bd0ab7)+ 纯补覆盖(边界全部钉死) | RED:`REQUIRE( roundSignificant(-0.0)=="0" ) with expansion: "-0" == "0"`(/tmp/red_wp_a.log);GREEN×2 103/103 |
| WP-B jsoncpp 无类型访问清点(#1447 同型) | 全模块逐访问点核对,三处真实逃逸:requireCount/readCount uint64 溢出、asInt 超界×4 站点、checks_artifact kind asString 穿出 runSpec | 可复现已修(commit 92edf5a86);其余访问点全部有守卫(TEST_MATRIX 清点表) | RED:`unexpected exception with message: "LargestUInt out of Int64 range"` 与 `"Type is not convertible to string"`(/tmp/red_wp_b.log);GREEN×2 105/105 |
| WP-C checks_* 七族 fail-closed 审计 | 缺失/漂移/无法取证/取消/异常/部分结果逐族找反例 | 未复现(除 WP-B 第 3 项,已归 WP-B 修复);明确不给 runSpec 加 blanket try/catch(providers.h 契约:provider 返回 Availability 而非抛) | IMPLEMENTATION.md WP-C 节;adversarial lane 20 用例 + B-3 |
| WP-D digest 确定性(键序打乱 metamorphic) | 16 键×16 种插入序同文本同 digest;重复 run 同 reportDigest;specDigest 序无关;无时间戳源 | 未复现(实现已正确),已补 property 测试钉死 | commit 6058c2050;GREEN×2 110/110 |
| WP-E locale 独立性 | comma-decimal 全局 locale(证明生效)下 canonical 文本/digest 不变 | 未复现(实现已正确,imbue classic),已补测试 | commit 6058c2050;GREEN×2 110/110 |
| WP-F 性能预算 | 5 万节点档 /usr/bin/time -v 实测(本机无该二进制,改 zsh time %M,等效) | 放弃优化+理由:0.10s/16192 KB(含启动),单遍 O(n) 写法,无 O(n²)/重复复制 | PERFORMANCE.md |
| WP-G packs_builtin/render 事实字段 | 三个内置 pack params 用求值器不消费的词汇(expect_present/required_fields/顶层 algorithm 等)→ 全部检查永远 Indeterminate | 可复现、按轨道归属放弃在本轨道修复:fail-closed 无误报 PASS(G 测试钉死);改词汇属 authoring 轨道(R14-E spec_runtime gap),仓内无运行时消费者 | commit 6058c2050;IMPLEMENTATION.md WP-G 节 |
| WP-H #1509 复查 | git diff 46791bc65^1..46791bc65 -- src/verification | 未复现(无语义漂移):改动仅 canonicalJson 顶层异常屏障,序列化语义零变化 | IMPLEMENTATION.md WP-H 节 |

旧 Issue 四态:#1488 已修复附证据(#1509 主体 + 本轮 WP-A/B 补族内残余);
#1447(verification 部分)可复现已修(WP-B 三处)。

## 已知起点线索(须重新实证,不得盲修)

1. #1488(canonical_json asInt64 溢出):#1509 已合并;当前 canonical_json.cpp 有完整
   isInt64/isUInt64 分支、非有限拒绝、深度/元素/字符串/输出预算与异常屏障。T01 须:
   a) 核验七个 lane 测试是否已覆盖这些边界(缺则补 property/差分回归);
   b) 审计 checks_*/report/pack 中其余 jsoncpp 无类型访问是否仍有逃逸(#1447 同型)。
2. roundSignificant=12 位有效数字是模块契约;digest 必须经 canonicalJson。
3. 跨轨道事项(记录,不越界修):faultlab/util/canonical_json.cpp 对非有限数写
   "NaN"/"Infinity" 进 digest 且无预算 —— 与本模块 fail-closed 语义相反,归 T02 处理。

## 停机规则

- 连续 3 次尝试无进展 → 换假设/缩小范围;8 次 → 重验前提并报告集成 Agent。
- 每个改动必须有先 RED 后 GREEN×2 的回归;不虚构提升数据。

## 收尾状态(R16-A-T01)

- 提交:357bd0ab7(WP-A)、92edf5a86(WP-B)、6058c2050(WP-C/D/E/G/F 覆盖)、文档提交(本目录)。
- 最终:110/110 GREEN×2(基线 100 + 新增 10);新 lane `test_verifier_r16a_14`。
- 跨轨道事项(勿在其它轨道顺手修本模块):
  1. builtin packs params 词汇 ↔ 求值器契约不匹配(WP-G)→ authoring/spec_runtime 轨道
     (R14-E 残留):决定替换层或改词汇,届时 G 测试需同步更新;
  2. faultlab/util/canonical_json.cpp 非有限数写 "NaN"/"Infinity" 且无预算(ledger 原有
     线索,归 T02,与本模块 fail-closed 语义相反);
  3. ISO-8601 语法分歧 grader↔resolver(R14-E 残留,均在 grader 侧,fail-closed,归相应轨道)。
