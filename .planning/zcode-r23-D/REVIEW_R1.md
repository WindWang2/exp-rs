# REVIEW_R1 — R23-D 独立评审（R1，新上下文，与实现者无关）

- 评审人：R1（独立评审轨道，全新上下文）
- 日期：2026-10-27
- 评审对象：`git diff origin/master...HEAD`，BASE = `ad98a7a70`（origin/master），HEAD = `7365c991a`（3 commits）
- worktree：`C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z`（分支 `agent/r23-d-lab-grading-evidence-20261026T165151Z`）
- 方法：全部结论亲自执行命令取证；`.planning/` 自评账本未采信（仅 BASELINE.md 取环境事实：MSVC 14.38 / Ninja / Qt 6.8.0 / vcpkg pinned `7f3781e19` / jsoncpp 1.9.6 经 `build-D/vcpkg_installed/x64-windows/include/json/version.h:12` 直接复核）。
- 评审开始时的工作区状态（非干净，先记录、全程保护、未动）：

  ```
  $ git status --porcelain   # 评审开始时
   M .planning/zcode-r23-D/tools/run_tests.cmd
   M tests/CMakeLists.txt
  ?? .planning/zcode-r23-D/TEST_RESULTS.md
  ?? .planning/zcode-r23-D/tools/run_exe.cmd
  ```

  即开发者有**未提交**的收尾改动（见 F1/F3）。本评审的所有突变均已还原：结束时 `git diff HEAD -- src/lab/session_store.cpp src/lab/spec_runtime.cpp data/labs/grading/ndvi_landsat.rules.json` 为 0 行，全量 `-R r23_d` 复绿（见 §B.6）。

---

## A. 逐文件 / 逐 hunk 审查

### A.1 `src/lab/session_store.cpp`（+14/−1，readJsonFile 补 try/catch）

**结论：正确，无缺陷。**

- 契约核验（不盲信背景契约）：`build-D/vcpkg_installed/x64-windows/include/json/reader.h:338-342` 明文 "Exceeding stackLimit (recursive depth of `readValue()`) will cause an **exception**"；`value.h:68-104` 确认 `Json::Exception` 是 `RuntimeError`/`LogicError` 的公共基类；`version.h:12` = `JSONCPP_VERSION_STRING "1.9.6"`。catch `const Json::Exception&` 同时覆盖深度炸弹（RuntimeError）与 LogicError，与 `src/lab_pack/lab_pack.cpp:252-263` 的全仓既有纪律一致。
- hunk 本身：异常路径 `error = "invalid JSON: " + exception.what()` → `loadFromPath` 包装为 `LabDiag{ "lab.session.corrupt", ... }`，fail-closed、无静默吞并；`out = std::move(doc)` 仍在成功路径，返回语义不变。字符串拼接用 `std::string(...) + exception.what()` 避免了 `const char* + std::string` 陷阱，类型正确。
- 覆盖面：`readJsonFile` 是 `loadFromPath` 的唯一文件读入口，`loadAny`（:276）与 `listSessions`（:326）都经它，修复同时保护公共 API load 与 listing 扫描。无内存/生命周期/线程问题（全部局部对象）。
- 突变检验（§B.1）：删 catch → 深度炸弹用例确实转红（`Exceeded stackLimit in readValue().` 逃逸），证明该 hunk 是测试红绿的真正原因。

### A.2 `src/lab/spec_runtime.cpp`（+12/−0，expected_numeric 有限性门）

**结论：正确，无缺陷。**

- hunk 位于 `asDouble()` 赋值之后、`min <= max` 检查之前，顺序正确：先拒非有限值，再谈区间方向。`question.hasExpectedNumeric = true` 先被置位但 `continue` 丢弃该 question（不入 `m_questions`/`m_plan.questions`），无残留影响。
- 诊断类型 `lab.runtime.field` 符合 spec_runtime.h「解析永不抛异常、全部 typed lab.runtime.*」契约（该解析路径本就不抛，本 hunk 只是新增一条 typed 拒绝，未引入 throw）。
- NaN 不可能来自 JSON 字面量（jsoncpp 只会把溢出字面量映成 ±inf），isfinite 同时覆盖 ±inf 与潜在 NaN，足够。
- 突变检验（§B.4）：删门 → 三组 ±inf 用例转红，证明辨别力真实。
- 回归：`ctest -R "lab_runtime|lab_grading|lab_pack"` 15/15 通过（§B.6），既有语料无被误伤的合法 spec。

### A.3 `data/labs/grading/ndvi_landsat.rules.json`（新增 83 行）

见 §C 逐项校验（全部通过）与 §B.2 突变（钉值被测试真实消费）。

### A.4 `tests/test_r23_d_ct6_lab_flow.cpp`（新增 229 行）

见 §D。要点：三个 oracle 均非空断言；负控真实走 FAIL 路径；发现的问题只有声明措辞（F2）与断言次序备注（F6），无正确性缺陷。跨模块契约抽查属实：

- `tools/sample_foundry.h:92-116`：`Options{ profile=Lab, seed=42 }` 默认与测试注释一致；确定性契约在同文件 :10-16 声明，且 `tests/test_sample_fixtures.cpp` 有独立覆盖（含 CLI byte-identical determinism）。
- `rs_spectral_index_operator.h:82-87` 的 `runSpectralIndexCore` 签名与测试调用一致（`allowIndexOverride=true` 仅通用 operator 合法，注释明示）；`rs_spectral_index_operator.cpp:966` 证实生产 operator 委托该 core，测试绕过注册表直连生产核心的声明成立。
- `packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json` 的 params（`index=NDVI, nir=5, red=4`）与测试硬编码一致。

### A.5 `tests/test_r23_d_lab_hardening.cpp`（新增 226 行）

见 §D。深度炸弹 200 层 > stackLimit 64，控制组（语法坏文件）走 parse-false 路径同码，正控（validV3Doc 仍 ok、有限反序界仍拒）齐全。仅 §D.3 的临时目录泄漏小瑕疵（P3）。

### A.6 `tests/CMakeLists.txt`（+43 行，纯 append）

- committed diff `grep -c '^-[^-]'` = 0（无删改行，见 §E 输出），落在 R22-04 注册块之后，符合 append-only。
- **但 committed 版缺 `TEST_PREFIX`**（详见 F1，P1）：`sicnu_discover_tests`（tests/CMakeLists.txt:41-55）自 D15 起支持 TEST_PREFIX，目的注释明写 "track gates can select cases by binary name (`ctest -R \"test_xxx::\"`)"。committed 版两处均未传。
- 链接面：ct6 链 Catch2/Qt6::Core/GDAL/sicnu_agent/sicnu_operators/sicnu_sample_foundry + jsoncpp，hardening 仅链 Catch2 + sicnu_lab_runtime，最小依赖，无越权链接。

### A.7 `.gitignore`（+8 行，纯 append）

模式与 r22-infra 既有条目同构（`!dir/` + `dir/*` + 白名单子集），只放行 `.planning/zcode-r23-D/` 下 `*.md` 与 `tools/*.{cmd,sh}`，build 产物不入库。纯新增，无既有规则改动。合规。

---

## B. 对抗突变检验（全部亲自执行，全部还原）

构建/运行命令（按评审协议）：
`cmd //c ".planning\zcode-r23-D\tools\build4.cmd --target <t>"`；`cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R r23_d"`；`cmd //c ".planning\zcode-r23-D\tools\run_exe.cmd <exe>"`（exe 实际位于 `build-D/` 根）。

### B.0 基线（突变前）

```
$ cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R r23_d"
1/5 test_r23_d_ct6_lab_flow::ct6: foundry lab-1 student flow grades PASS ... Passed 0.67s
2/5 test_r23_d_ct6_lab_flow::ct6: wrong-CRS submission fails with a typed grid localization ... Passed
3/5 test_r23_d_ct6_lab_flow::ct6: dual authority guard ... Passed
4/5 test_r23_d_lab_hardening::session store: depth-bomb ... Passed
5/5 test_r23_d_lab_hardening::spec runtime: expected_numeric bounds must be finite ... Passed
100% tests passed, 0 tests failed out of 5
```

### B.1 突变 1：删除 session_store.cpp 的 catch（恢复 master 行为）→ 预期红

操作：readJsonFile 恢复为无 try/catch 的 master 版本，重建 `test_r23_d_lab_hardening`：

```
$ cmd //c ".planning\zcode-r23-D\tools\run_exe.cmd test_r23_d_lab_hardening.exe"
test_r23_d_lab_hardening.cpp(145): FAILED:
due to unexpected exception with message:
  Exceeded stackLimit in readValue().
test cases:  2 |  1 passed | 1 failed
```

**判定：深度炸弹用例如约转红**（异常真实逃逸 loadAny，非空断言）；同文件 spec-runtime 用例不受影响（隔离正确）。控制组（`{"a": nope}` 语法坏文件同码 `lab.session.corrupt`）证明 catch 没有掩盖 parse-false 路径。

### B.2 突变 2：ndvi_landsat.rules.json mean 钉值 0.4521261 → 0.7521261 → 预期红

```
$ cmd //c ".planning\zcode-r23-D\tools\run_exe.cmd test_r23_d_ct6_lab_flow.exe"
test_r23_d_ct6_lab_flow.cpp(146): FAILED:  REQUIRE( grade.verdict == "pass" )
with message: score: 59 verdict: fail error:
test_r23_d_ct6_lab_flow.cpp(177): FAILED:  REQUIRE( control.verdict == "pass" )   # 负控的正对照组
test cases:  3 |  1 passed | 2 failed
```

**判定：PASS oracle 转红。** score 59 = 100 − 30（mean_sigma 失败）后被 blocking 截到 passingScore−1，证明：(1) 钉值被测试真实消费，非空断言；(2) mean_sigma 的 blocking 截断语义被顺带验证。双权威守卫用例不受影响（用 ndvi_basics），隔离正确。

### B.3 突变 3（加分）：grid.compatible severity `blocking` → `normal` → 检查负控

```
$ cmd //c ".planning\zcode-r23-D\tools\run_exe.cmd test_r23_d_ct6_lab_flow.exe"
test_r23_d_ct6_lab_flow.cpp(185): FAILED:  REQUIRE( grade.verdict == "fail" )
test cases:  3 |  2 passed | 1 failed
```

**判定：负控对 severity 降级敏感（转红），但第一道防线是 :185 的 verdict 断言而非 :187 的 `cappedByBlocking`。** 失败机理：grid 权重 10、非 blocking → score 90 ≥ 60 且无截断 → verdict 变 "pass"，verdict 断言先失败并中止用例；若无 :185/:186，:187 `REQUIRE(grade.cappedByBlocking)`（此时为 false）与 :186 `score < passingScore`（90 < 60 为假）也会失败——三重防线，非恒真。`grid->severity == "blocking"`（:191）此时仍会通过（它读 deductions 记录里的 severity 字段，降级后该记录 severity 就是 "normal"，故 :191 也会红）。

### B.4 突变 4（额外）：删除 spec_runtime.cpp 的 isfinite 门 → 预期红

```
$ cmd //c ".planning\zcode-r23-D\tools\\run_exe.cmd test_r23_d_lab_hardening.exe"
test_r23_d_lab_hardening.cpp(137): FAILED:  REQUIRE( !result.ok )   with expansion: false
test cases:  2 | 1 passed | 1 failed
```

**判定：±inf 界用例转红**——master 行为（vacuous acceptance，`[-inf,+inf]` 全收）被 oracle 精确捕获。

### B.5 还原与复核

```
$ git diff HEAD -- src/lab/session_store.cpp src/lab/spec_runtime.cpp data/labs/grading/ndvi_landsat.rules.json | wc -l
0
$ cmd //c ".planning\zcode-r23-D\tools\build4.cmd --target test_r23_d_lab_hardening test_r23_d_ct6_lab_flow"
[4/4] Linking CXX executable test_r23_d_lab_hardening.exe
$ cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R r23_d"
100% tests passed, 0 tests failed out of 5      （Label Time Summary: FAST = 1.83s, 5 tests）
$ cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R \"lab_runtime|lab_grading|lab_pack\""
100% tests passed, 0 tests failed out of 15     （相邻套件无回归）
```

临时备份（/tmp/*.orig）已删除；工作区回到评审开始时的状态（仅既有的 4 条未提交项 + 本文件）。

---

## C. 规则文件正确性（对照 output_verifier.cpp validateRules 逐项）

对照代码：`src/agent/output_verifier.cpp`（parseRules :536-701、per-kind params :700-901、kKnownKinds :347-356、kRulesSchemaVersion :345、stem 校验 :581、resolveRulesPath :904-926、评分/blocking 截断 :2399-2461、digest :2500-2583）。

| 校验项 | 规则要求（代码行） | ndvi_landsat 实测 | 判定 |
|---|---|---|---|
| schema_version | `== "sicnu.lab.rules/1"`（:345,:567） | `"sicnu.lab.rules/1"` | OK |
| lab_id 非空（:574） | 非空 | `"ndvi_landsat"` | OK |
| 文名词干 = lab_id + ".rules"（:581） | `completeBaseName == lab_id + ".rules"` | `ndvi_landsat.rules` == `ndvi_landsat` + `.rules` | OK（不会被 :581 拒） |
| title 非空 | 非空 | 有 | OK |
| artifact.kind ∈ {raster,file} | :594 | `"raster"` | OK |
| passing_score ∈ [0,100]（:604） | 数值 | 60 | OK |
| assertions 非空数组、id 唯一 | :612,:640 | 5 条、id 唯一 | OK |
| kind ∈ kKnownKinds（:347） | 9+7 种 | crs_grid / range / mean_sigma / nodata_ratio / histogram_shape 全在列 | OK |
| severity ∈ {normal,blocking}（:663） | — | blocking×3, normal×2 | OK |
| weight ∈ (0,100] 且总和 = 100（:675,:692） | `|sum−100| ≤ 1e-9` | 10+15+30+15+30 = **100 精确** | OK |
| crs_grid params（:868-881） | epsg>0, w/h>0, ps≠0, band_count≥1, tol≥0 | 32648 / 256 / 256 / 30.0 / 30.0 / 1 / 1e-06 | OK |
| range params（:715-727） | min<max, ratio∈[0,1] | −1<1, 0.0 | OK |
| mean_sigma params（:729-745） | mean_tol>0, σmin≤σmax, σtol≥0 | 0.4521261±0.01, [0.48,0.54], 0.0 | OK |
| nodata_ratio（:760-767） | ∈[0,1], min≤max | [0.001, 0.05] | OK |
| histogram_shape（:770-785） | band 必需, bins∈[2,4096], min<max, shape 枚举, fraction∈[0,1], sep≥0 | band 1, 64 bins, bimodal, 0.05, 0.5 | OK |

**双权威互斥性（ndvi_landsat vs ndvi_basics，同目录共存）**：lab_id 不同（`ndvi_landsat` / `ndvi_basics`）、文件名不同、各自通过 :581 词干一致性；`resolveRulesPath(labId)` 按 `<labId>.rules.json` 精确拼名，无遮蔽。`resolveRulesPath` 顺序核验属实：rulesDir 参数 > `SICNU_LAB_RULES_DIR` > `SICNU_SOURCE_DIR/data/labs/grading`（:909-917）；ct6 测试不传 rulesDir、不设 env，落在第三级源码目录。

**校准来源核验**：与 `packaging/bundle/labs/grading-overlay/ndvi_basics.rules.json`（bundle 侧 foundry 校准权威）逐参数对比——grid（32648/256/30m/1e-06）、range、mean_sigma（0.4521261/0.01/[0.48,0.54]/0.0）、nodata（[0.001,0.05]）、histogram 全部一致；仅 lab_id（共存必要）、title、derivation 措辞不同。"Calibration identical to the offline-classroom bundle overlay" 的声明属实。fixture 权威 `data/labs/grading/ndvi_basics.rules.json` 零改动（不在 diff 中），其 reference corpus = 100 语料（tests/test_lab_grading.cpp:271,309）不受影响——由 §B.5 的 lab_grading 15/15 实测背书。

**passing_score=60 与权重和=100**：上表核验；负控截断语义（`score = min(score, passingScore−1)`，:2448）在突变 2 中被实测观察（59 分）。

---

## D. 测试自身质量

### D.1 无恒真断言（逐条过）

- ct6 PASS oracle：`graded`/`verdict=="pass"`/`score>=passingScore`/`score==100.0`/`deductions.empty()`——`score==100` 等值钉在突变 2 下真实失败（59）；`deductions.empty()` 同理可红。
- ct6 负控：`verdict=="fail"`/`score<passingScore`/`cappedByBlocking`/deduction 存在性/severity/精确消息 `CRS EPSG:4326 does not match expected EPSG:32648`（与 output_verifier.cpp:1555 格式一致）/结构化 observed-expected——突变 3 下 :185 转红；`cappedByBlocking` 在降级场景为 false 亦会红（被 :185 先行中止）。
- 双权威守卫：fixture=100 精确（若 ndvi_basics 规则漂移即红）+ foundry 工件 < 60（若 ndvi_landsat 校准漂向 fixture 场景，PASS oracle 或此条之一必红）——两个方向的漂移都被夹住。
- hardening：`firstDiagCode` 内嵌 `REQUIRE(!result.ok)` + `REQUIRE(!diagnostics 非空)`，非恒真（突变 4 下 !ok 失败）；消息含 "stackLimit" 断言依赖异常文本透传，突变 1 证明其可红。
- 每个用例都带正控（valid doc ok / 未改工件 pass / 语法坏文件同码），防"全拒绝"回归。

### D.2 负控真实走 FAIL 路径

基线运行 2/5 通过的 wrong-CRS 用例要求 `verdict=="fail"` 且找到 `grid.compatible` deduction 且 severity/message/observed/expected 全部匹配——这是 graded-FAIL（走完评分管线），不是 error/refusal 路径。突变 3 进一步证明该路径的分数语义（90 ≥ 60 时 verdict 翻 pass）被断言网罩住。

### D.3 双跑 digest 一致性断言的辨别力

digest = SHA-256 over canonical body（output_verifier.cpp:2500-2583：lab_id/artifact/rules 路径/verdict/score/capped/deductions/evidence/summary，无时钟）。同工件同路径双跑相等 → 任何评分、证据或序列化的非确定性都会翻红；有辨别力。**局限（P3）**：仅同进程同工件路径的 intra-run 一致性；foundry 跨进程/跨主机的字节确定性不由此断言守卫，而由 `tests/test_sample_fixtures.cpp`（byte-identical determinism 语料）+ manifest sha256 机制守卫——分工合理，非缺陷。

### D.4 小瑕疵

- hardening 用例的 `std::filesystem::remove_all(root)` 在任一 REQUIRE 失败时被跳过（Catch2 中止用例），失败路径泄漏 `%TEMP%\r23d-bomb-*`（P3，与仓内惯用写法一致）。
- ct6 输出的 OpenCV parallel plugin `libraryLoad ... => FAILED` INFO 行为环境噪声（构建树无 onetbb/tbb/openmp 插件 DLL），非测试缺陷。

---

## E. 跨轨污染检查

```
$ git diff origin/master...HEAD --name-only   （23 个文件，逐一比对白名单）
OK   .gitignore / .planning/zcode-r23-D/**(15) / data/labs/grading/ndvi_landsat.rules.json
OK   src/lab/session_store.cpp / src/lab/spec_runtime.cpp
OK   tests/CMakeLists.txt / tests/test_r23_d_ct6_lab_flow.cpp / tests/test_r23_d_lab_hardening.cpp
VIOL （0 个）
$ git diff origin/master...HEAD --name-status | grep -c "^D"        → 0（无删除）
$ git diff origin/master...HEAD -- tests/CMakeLists.txt | grep -c "^-[^-]"  → 0（纯新增）
$ git diff origin/master...HEAD -- .gitignore | grep "^-" | grep -v "^---"  →（无输出，纯新增）
```

**结论：白名单 0 违例；tests/CMakeLists.txt 与 .gitignore committed diff 均纯 append（0 行删除/修改）；src/teaching、src/app、src/verify、packaging、scripts、cmake 等禁写区零触碰。**

但注意（引出 F1）：工作区未提交的 `tests/CMakeLists.txt` 修改仍只动本轨新增的 2 行（给两处 `sicnu_discover_tests` 加 `TEST_PREFIX`），对 master 仍属 append-only 范畴，不构成跨轨污染——问题是它没被提交（F1）。

---

## F. 发现清单（P0 必须修 / P1 应修 / P2 建议 / P3 可延后）

**P0：无。**

**F1（P1，应修，一行×2）：committed 的测试注册缺 `TEST_PREFIX`，`ctest -R r23_d` 门在提交态空转；修复躺在工作区未提交。**
- 证据链：
  1. 未加前缀的注册，ctest 名就是裸 Catch2 名（同仓对照：`ctest -N` 中 test_lab_grading 的名字是 `lab_grading.reference corpus ...`，无二进制名前缀）。
  2. 本轨 5 个 Catch2 用例名（`ct6: ...`×3、`session store: ...`、`spec runtime: ...`）**均不含子串 `r23_d`** → 提交态下 `ctest -R r23_d` 命中 0 个测试，轨道门空转通过（假绿）。
  3. 当前能选到 5 个测试，是因为工作区未提交 diff 给两处 `sicnu_discover_tests(...)` 加了 `TEST_PREFIX test_r23_d_ct6_lab_flow::` / `TEST_PREFIX test_r23_d_lab_hardening::`（`git diff tests/CMakeLists.txt` 可见，仅改本轨新增行）。
  4. `sicnu_discover_tests` 的 TEST_PREFIX 参数（tests/CMakeLists.txt:42-43）自 D15 起就为轨道门选择而设——这是仓内既定惯例，本轨提交版未遵守。
- 修复：把工作区的两行 TEST_PREFIX 改动提交进本轨分支（或等价 amend）。合入前必须落库，否则全量 CI 之外的门禁/复跑选择对该 5 用例失效。

**F2（P2，建议）：CT-6「closure」措辞超出实际交付面；run_lab.cmd 学生流程在合入后依旧 45/100。**
- 事实：`scripts/windows/run_lab.cmd` 第 3 步仍是 `lab --lab ndvi_basics --grade output\lab1_ndvi.tif`，而第 1/2 步产出的是 foundry 场景 → 新生首跑仍 FAIL（本轨双权威 oracle 自己就断言该工件在 ndvi_basics 下 fail）。
- 测试头注释（"the repo-documented lab-1 student flow ... must grade PASS"）与 rules derivation（"closing R21-E CT-6 for the repo-documented run_lab.cmd flow"）读起来像端到端收口，实际交付的是「repo 侧存在可 PASS 的权威 + 回归测试」；`scripts/`、`packaging/` 在本轨白名单外，无法真正改脚本——开发者已在 `CROSS_TRACK_FINDINGS.md` CT-D1 如实披露并建议 E 轨一行改 `--lab ndvi_landsat`。
- 处置：合入时可保留，但 PR 描述/测试注释应避免"run_lab.cmd flow 已收口"的读法；**residual owner：E 轨（scripts/ + tools/），一行改动**。

**F3（P2，建议）：交付卫生——收尾改动未提交，且 committed 版轨道工具跑不起本轨 e2e。**
- 工作区残留：`run_tests.cmd` 的 PATH 补丁（加 `C:\deps\Qt\...\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin`）、新增 `tools/run_exe.cmd`、`TEST_RESULTS.md`，全部未提交。实测 `build-D/` 根与 `build-D/tests/` 均无 Qt6Core.dll（只有 vcpkg DLL），ct6 e2e 依赖 PATH 里的 Qt bin——即 **committed 版 run_tests.cmd（PATH 只有 vcpkg+build-D）启动不了 test_r23_d_ct6_lab_flow.exe**，本轮评审实际用的是未提交版。
- 处置：这些都在 `.planning/zcode-r23-D/` 白名单内，直接补提交即可（与 F1 一并收尾）。

**F4（P3，可延后）：ct6 的 digest 双跑只证 intra-run 确定性。** foundry 跨运行字节一致性由 test_sample_fixtures 独立守卫（已存在），分工合理；如想更强，可在两个独立生成目录上比对 grade digest。residual owner：D 轨后续或 maintainer。

**F5（P3，可延后）：hardening 用例失败路径泄漏临时目录**（REQUIRE 失败即跳过 remove_all）。与仓内惯用写法一致，影响仅 %TEMP% 堆积。residual owner：D 轨后续。

**F6（P3，可延后，记录性）：突变 3 显示负控对 severity 降级的第一道防线是 :185 verdict 断言，`cappedByBlocking`(:187) 与 `score < passingScore`(:186) 在该突变下是二、三道防线**（被 :185 先行中止）。三重覆盖无恙真风险，仅备注断言次序的归因顺序。

**F7（P3，记录性，正面）：catch `Json::Exception`（而非更窄的 `Json::RuntimeError`）同时捕获 LogicError**——与 lab_pack.cpp:259 等全仓纪律一致，对不可信盘上内容取宽 catch 是正确取舍，非缺陷。

---

## G. 总体结论

**PROCEED-WITH-FIXES（建议合入，先落 F1）。**

- 代码正确性：两处 src/lab 改动（session_store 深度炸弹 typed 拒绝、spec_runtime 有限界门）小而准，契约经头文件独立核验，4 组对抗突变全部按预期转红、全部还原、相邻套件（lab_runtime/lab_grading/lab_pack 15/15）无回归——**新测试的辨别力是实测的，不是声称的**。
- 规则文件：ndvi_landsat.rules.json 对 validateRules 全项通过（schema/词干/枚举/权重和=100 精确/各 params 域），与 bundle overlay 校准逐参数一致，与 fixture 权威 ndvi_basics 双向互斥且互不遮蔽，resolveRulesPath 顺序核验属实。
- 测试质量：无恒真断言，正负控齐备，负控走真实 graded-FAIL 路径，digest 断言有辨别力。
- 越界检查：白名单 0 违例，tests/CMakeLists.txt 与 .gitignore committed diff 纯 append，无删除。
- 必办（合入前）：**F1**——提交 TEST_PREFIX 两行（否则 `-R r23_d` 轨道门对提交态空转，这正是本次评审靠未提交工作区改动才跑出 5/5 绿的隐蔽风险）；顺带收 F3 的未提交工具/文档。
- 建议办：F2 措辞校准 + 把 CT-D1 的 E 轨一行修复带进 PR 描述，避免下游误读 CT-6 已端到端收口。

—— R1 独立评审人，2026-10-27。本评审除本文件外未修改/提交任何文件；所有突变已还原（`git diff HEAD` 于三个被突变文件为 0 行）；未 push。
