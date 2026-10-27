# REVIEW_R2 — R23-D 独立评审（R2，第二位、全新上下文，与实现者及 R1 均无关）

- 评审人：R2（独立评审轨道，全新上下文）
- 日期：2026-10-27
- 评审对象：完整 HEAD diff（`git diff origin/master...HEAD`，4 个提交：`069fc43c4` → `2be7bc258` → `7365c991a` → `ed30c9790`），BASE = `ad98a7a70`（origin/master）
- worktree：`C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z`（分支 `agent/r23-d-lab-grading-evidence-20261026T165151Z`）
- 方法：全部结论亲自执行命令取证；`.planning/` 自评账本（含 TEST_RESULTS.md）未采信为证据；REVIEW_R1.md 仅作对照对象，其关键声明逐一独立重验（见 §A），并专查其漏洞与修复提交回归（见 §B/C）。
- 评审开始时 `git status --porcelain` 为空（干净树）；结束前同样必须只余本文件未跟踪。未 push。

---

## A. 对 R1 关键声明的独立重验

| R1 声明 | R2 重验结果 |
|---|---|
| F1（P1）：committed 缺 TEST_PREFIX，`-R r23_d` 空转 | **已修复并经我独立验证**：`git show ed30c9790 -- tests/CMakeLists.txt` 两行 TEST_PREFIX 落库；`run_tests.cmd -N -R r23_d` 列出恰好 Test #1311–#1315（3 ct6 + 2 hardening），`Total Tests: 5`；`-R r23_d` 实跑 5/5 Passed |
| 突变：去 catch → 深度炸弹红 | **复现**（我独立重做，§B.1） |
| 突变：mean 钉值 → PASS oracle 红 | **换轴复现**（我改 sigma 窗口而非 mean，§B.2，机制相同） |
| 突变：删 isfinite 门 → ±inf 红 | **复现**（我独立重做，§B.3） |
| 规则文件 validateRules 全项通过、与 bundle overlay 校准一致 | **独立重查通过**（§D，逐参数 python 比对，非抄 R1 的表） |
| resolveRulesPath 三级顺序 | **属实**（output_verifier.cpp:904-926 亲读：rulesDir > `SICNU_LAB_RULES_DIR` env > `SICNU_SOURCE_DIR/data/labs/grading`；4 个 wrapper 均不设该 env；我的规则文件突变被测试真实消费 = 第三级解析的活体证明） |
| catch `Json::Exception` 契约 | **属实**（jsoncpp 1.9.6 header/行为经突变 1 实证：异常真实逃逸路径存在） |
| **§B.5 "lab_runtime\|lab_grading\|lab_pack 15/15"** | **不可按原样复现，R1 证据有瑕疵（见 F2-R2）**：带引号交替正则经 cmd wrapper 传给 ctest 会带上字面 `"`（实测 `-N -R "r23_d|lab_grading"` → `Total Tests: 0`）；R1 打印的命令原样执行不可能得到 15。实质性结论仍成立：`-R lab_grading` 单独实跑 **15/15 Passed**（FAST 9.85s），`-R runtime.block` + `-R v3.document`（test_lab_runtime 的 runtime block 套件，spec_runtime 真正的相邻面）**5/5 Passed**。而 `-R lab_pack` 实际命中 2 个与本轨无关的测试：#948（既有 Windows 环境失败，见 F3-R2）与 #1179 NOT_BUILT 占位（build-D 为部分构建树）。 |

## B. 对抗突变检验（全部亲自执行，全部还原，复绿）

### B.0 基线（HEAD，突变前）

```
$ cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R r23_d"
100% tests passed, 0 tests failed out of 5      (FAST = 4.12 sec*proc, Total 4.87s)
```

### B.1 突变 1：readJsonFile 恢复 master 无 catch 行为 → 预期红

重建 `test_r23_d_lab_hardening` 后：

```
test_r23_d_lab_hardening.cpp(153): FAILED:
due to unexpected exception with message:
  Exceeded stackLimit in readValue().
test cases:  2 |  1 passed | 1 failed
```

**判定：深度炸弹用例如约转红**——catch 是该用例红绿的真实原因；同文件 spec-runtime 用例不受影响（隔离正确）。

### B.2 突变 2：ndvi_landsat.rules.json `sigma_min` 0.48 → 0.53（R1 突变的是 mean 钉值，此处换轴）→ 预期红

```
test_r23_d_ct6_lab_flow.cpp(148): FAILED: REQUIRE( grade.verdict == "pass" )
  score: 59 verdict: fail
test_r23_d_ct6_lab_flow.cpp(197): FAILED: REQUIRE( control.verdict == "pass" )   # 负控的正对照组
test cases:  3 | 1 passed | 2 failed
```

**判定：PASS oracle 转红。** 59 = 100 − 30（mean_sigma 失败）后被 blocking 截到 passingScore−1，sigma 窗口钉值被测试真实消费；双权威守卫用例（ndvi_basics）不受影响（隔离正确）。顺带实证 blocking 截断语义（output_verifier.cpp:2448）。

### B.3 突变 3：spec_runtime.cpp isfinite 门包进 `if(false)` 禁用 → 预期红

```
test_r23_d_lab_hardening.cpp(209): FAILED:
  REQUIRE( firstDiagCode(...) == "lab.runtime.field" )
  (首道防线 REQUIRE(!result.ok) 以 false 扩张失败 —— master 的 vacuous acceptance 被精确捕获)
test cases:  2 | 1 passed | 1 failed
```

### B.4 还原与复绿

```
$ git checkout HEAD -- src/lab/session_store.cpp src/lab/spec_runtime.cpp data/labs/grading/ndvi_landsat.rules.json
$ git diff HEAD -- <三个被突变文件> | wc -l          → 0
$ build4.cmd --target test_r23_d_lab_hardening test_r23_d_ct6_lab_flow   → 全部链接成功
$ run_tests.cmd -R r23_d                             → 100% tests passed, 0 tests failed out of 5
```

相邻面补充（非本轨门的回归面）：
- `-R lab_grading`：15/15 Passed（fixture 权威语料无回归）。
- `-R runtime.block` + `-R v3.document`（test_lab_runtime 中 spec_runtime 的相邻用例）：5/5 Passed。
- `run_exe.cmd test_lab_runtime.exe`：24/25，唯一失败是**既有宿主问题**（case "session store persists atomically..." 末尾 `remove_all` 抛 "另一个程序正在使用此文件" 的 Windows 文件锁异常，test_lab_runtime.cpp:898）。**归因实验**：换入 master 版 session_store.cpp 重编后同样 24/25 同点失败 → 与本轨 diff 无关（已还原、重编、复绿）。
- `-R lab_pack` 命中的 #948 见 F3-R2（与本轨 diff 零文件交集的既有失败）。

---

## C. 专查 R1 修复提交 ed30c9790（8 文件，每一 hunk 均读）

1. **TEST_PREFIX 两行落库**：✅ `sicnu_discover_tests(test_r23_d_ct6_lab_flow TEST_PREFIX test_r23_d_ct6_lab_flow:: TIMEOUT 300)` / `..._lab_hardening TEST_PREFIX test_r23_d_lab_hardening::`；`-N -R r23_d` 实选 5 例（#1311–1315）。wrapper 语义核对：`cmake_parse_arguments(SDT "" "TEST_PREFIX;TEST_SPEC" "")` 将 TEST_PREFIX 作单值关键字，`TIMEOUT 300` 落入 UNPARSED → 转发为 test property——与 `test_mlops9_evidence ... TEST_PREFIX ... TIMEOUT 300`、`test_experiment_*_r4 ... TIMEOUT 180` 的仓内既有组合完全同构。
2. **ct6 cross-run determinism 段**：✅ summary 结构核对（output_verifier.cpp:2459-2496 亲读）：raster 路径 `summary["width"] = meta->width`（:2468）、`summary["crs"] = meta->crs.authid`（:2471）——**扁平键**，与测试的 `rerun.summary["crs"].asString()` / `summary["width"].asInt()` 一致；artifact 为 raster（rules `artifact.kind=raster`）故键非缺失、断言非恒真（前面 `verdict`/`score`/`deductions.empty()` 先行把关）。**不做跨目录 digest 相等的理由成立**：`toBodyJson()`（:2540-2543）嵌入 `body["artifact"]`/`body["rules"]` 绝对路径，跨目录 digest 必然不同，内容级断言是正确替代。**QTemporaryDir second 生命周期安全**：作用于 test-case 内、在最后一次 gradeArtifact 之后析构，无悬挂引用；Catch2 REQUIRE 失败经栈展开仍触发其析构（autoRemove），不泄漏。
3. **RAII TempRootGuard**：✅ 析构用 `remove_all(path, ec)`（error_code 重载，不抛）；异常路径（REQUIRE 抛出）经栈展开清理；原测试尾部 remove_all 已删除，无 double-remove；makeTempRoot 若在 guard 构造前抛出则尚无目录可清（可忽略的边角）。
4. **rules derivation 措辞**：✅ python `json.load` 解析成功（合法 JSON）；与 `2be7bc258` 版逐 assertion params 机器比对 **完全一致**（`params identical: True`）——只动了 grid.compatible 的 derivation 字符串，参数零触碰。
5. **wrapper PATH 亲启验证**：✅ 本轮评审全程用提交态 wrapper：`run_tests.cmd` 多次跑 5/5、`run_exe.cmd` 亲启 `test_r23_d_ct6_lab_flow.exe`、`test_r23_d_lab_hardening.exe`、`test_lab_runtime.exe` 全部正常启动。PATH 必要性独立核实：`build-D/Qt6Core.dll` 与 `build-D/tests/Qt6Core.dll` 均不存在（只有 vcpkg DLL），ct6（qt_add_executable）确实依赖 `C:\deps\Qt\6.8.0\msvc2022_64\bin`——F3 修复必要且有效。
6. 修复提交无越界：8 文件全部在本轨白名单内（4 个 .planning + rules.json + tests/CMakeLists.txt + 两个测试源）。

---

## D. 规则文件与引擎契约（独立重查，不抄 R1 的表）

对照代码亲读：`src/agent/output_verifier.cpp`（kRulesSchemaVersion :345、kKnownKinds :347-358、stem 校验 :578-585、severity/weight/权重和 :663-696、per-kind params :700-901、resolveRulesPath :904-926、评分与 blocking 截断 :2399-2461、summary :2459-2496、digest body :2540-2583）。

| 校验项 | 引擎要求（亲读） | ndvi_landsat 实测（python 解析） | 判定 |
|---|---|---|---|
| schema_version | `== "sicnu.lab.rules/1"` | 同 | OK |
| lab_id 非空 / 文名词干 | `completeBaseName == lab_id + ".rules"` | `ndvi_landsat.rules` == `ndvi_landsat`+`.rules` | OK |
| artifact.kind | raster\|file | raster | OK |
| passing_score ∈ [0,100] | :604 | 60 | OK |
| assertions 非空、id 唯一 | :612,:640 | 5 条、唯一 | OK |
| kind ∈ kKnownKinds | :347 | crs_grid/range/mean_sigma/nodata_ratio/histogram_shape 全在 17 种枚举内 | OK |
| severity | normal\|blocking | blocking×3, normal×2 | OK |
| weight ∈ (0,100]、和=100（\|sum−100\|≤1e-9） | :675,:692 | [10,15,30,15,30]，sum=100 精确 | OK |
| crs_grid params | epsg>0,w/h>0,ps≠0,band_count≥1,tol≥0 | 32648/256/256/30/30/1/1e-06 | OK |
| range params | min<max, ratio∈[0,1] | −1<1, 0.0 | OK |
| mean_sigma params | mean_tol>0, σmin≤σmax, σtol≥0 | 0.01 / 0.48≤0.54 / 0.0 | OK |
| nodata_ratio | ∈[0,1], min≤max | [0.001, 0.05] | OK |
| histogram_shape | band 必需, bins∈[2,4096], min<max, shape 枚举, fraction∈[0,1], sep≥0 | 1/64/−1<1/bimodal/0.05/0.5 | OK |

- **双权威互斥**：同目录 `ndvi_basics.rules.json`（不在 diff 中，零改动）与 `ndvi_landsat.rules.json` lab_id、文件名均不同，各自通过 stem 校验；`resolveRulesPath` 按 `<labId>.rules.json` 精确拼名（:919），无遮蔽可能。
- **resolveRulesPath 三级顺序**：rulesDir 参数 > `SICNU_LAB_RULES_DIR` > `SICNU_SOURCE_DIR/data/labs/grading`（:909-917 亲读）；测试不传 rulesDir、4 个 wrapper 均不设 env → 落第三级；突变 2 对源码目录文件的修改直接翻转 oracle = 解析路径的活体证明。
- **校准来源**：与 `packaging/bundle/labs/grading-overlay/ndvi_basics.rules.json` 逐 assertion params 机器比对，5/5 **IDENTICAL**（grid/range/mean_sigma/nodata/histogram），仅 lab_id/title/derivation 不同——"repo-side copy of the foundry-scene authority" 声明属实。
- **测试↔引擎跨模块契约抽查**（亲读）：`packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json` params（index=NDVI, nir=5, red=4）与测试硬编码一致；`runSpectralIndexCore` 签名（rs_spectral_index_operator.h:82-89）与测试调用一致，`allowIndexOverride=true` 仅为通用 operator 合法（头文件注释明示），生产 `RsSpectralIndexOperator::run`（.cpp:966-970）同参委托；foundry `Options{profile=Lab, seed=42}`（tools/sample_foundry.h:104-110）与测试注释一致；foundry 字节级确定性由 tests/test_sample_fixtures.cpp 独立语料覆盖（:8,:15）。

## E. 跨轨污染（独立全量重查）

```
$ git diff origin/master...HEAD --name-only           → 26 文件
$ git diff origin/master...HEAD --name-status | grep -c "^D"        → 0
$ git diff origin/master...HEAD -- tests/CMakeLists.txt | grep -c "^-[^-]"  → 0   （纯 append）
$ git diff origin/master...HEAD -- .gitignore | grep -c "^-[^-]"    → 0          （纯 append）
白名单比对（src/lab|src/lab_pack|src/grader|src/recipes|src/faultlab|src/verification|
src/verify_adapters|data/labs|tests|.planning/zcode-r23-D|docs|.gitignore）：
  26/26 OK，VIOLATION = 0
```

`data/labs/grading/ndvi_basics.rules.json`（fixture 权威）不在 diff 中；tests/CMakeLists.txt 的 R23-D 块落在 R22-04 注册块之后，仅新增两个注册块，未触碰任何他轨行。

## F. R1 漏审面补查（任务 E）

1. **catch_discover_tests 参数惯例**：DISCOVERY_MODE PRE_TEST 由 wrapper 统一注入（tests/CMakeLists.txt:99-100），本轨无需也不能自带；TIMEOUT 300 + 默认 FAST 标签有仓内先例（test_mlops9_evidence）；ct6 实测 ~2-4s，300s 是 CI 冗余。hardening 无 TIMEOUT（CTest 默认 1500s）足够。✅
2. **.gitignore 白名单块真实行为**（`git check-ignore -v` 实测）：`.planning/zcode-r23-D/build/junk.obj` 被 `.planning/zcode-r23-D/*` 命中 = 忽略 ✅；`build-D/junk.exe` 被既有 `build-*/` 忽略 ✅；顶层 `*.md` 经 `!*.md` 放行（R1 评审期 `?? TEST_RESULTS.md` 可见即活证）。**注意**：`tools/` 下非 cmd/sh 文件（如假想的 tools/foo.exe）无任何 ignore 模式命中——不会被静默排除，而是以未跟踪状态显式出现在 `git status`（fail-loud 而非 fail-silent），与 r22-infra 既有块同构。记 P3（F5-R2）。
3. **并行约定**：run_tests.cmd 硬编码 `--parallel 1`（提交态）✅。两测试无需 RUN_SERIAL：ct6 三用例各持独立 QTemporaryDir（mkdtemp 语义唯一）、只读共享源数据；hardening 仅一个用例建临时根且 tag 唯一、另一用例不落盘；ctest 并行下同 binary 各用例为独立进程、无共享可变资源。（对照：R22-04 的 RUN_SERIAL TRUE 是该测试自身资源需求，非普适要求。）
4. **GBK 控制台 ASCII 安全**（em-dash 教训）：5 个 TEST_CASE 标题 + 两处 TEST_PREFIX 全部纯 ASCII（grep -P 非 ASCII 扫描：非 ASCII 字符只出现在注释与 validV3Doc 的中文字符串字面量中，均不进 ctest 名）；第三个标题用普通连字符 `-` 而非 em-dash。✅
5. **wrapper 环境**：4 个 .cmd 均不设 `SICNU_LAB_RULES_DIR`（grep=0）→ 规则解析不被环境劫持；`ctest -R` 值全程不加引号（cmd 传参，引号会成为正则字面量，见 F2-R2 的教训实证）。

## G. 发现清单与总体结论

**P0：无。P1：无。**

**F1-R2（P2，评审质量，非代码缺陷）：R1 的 §B.5 相邻套件证据命令不可复现。** 带引号的交替正则经 run_tests.cmd（cmd 批处理 %* 透传）传给 ctest 会保留字面引号：实测 `run_tests.cmd -N -R "r23_d|lab_grading"` → `Total Tests: 0`。故 R1 打印的 `-R \"lab_runtime|lab_grading|lab_pack\" → 15/15` 组合在其环境下不可能成立（15 恰是 `-R lab_grading` 单独的命中数；`lab_runtime` 匹配 0、`lab_pack` 匹配 2）。**实质无碍**：本评审用无引号单词正则重做了真实相邻面（lab_grading 15/15、runtime block 5/5、test_lab_runtime 24/25 且唯一失败经 master 对照定性为既有宿主文件锁问题），本轨 diff 的无回归结论反而被更严谨地证实。遗留：后续评审/CI 引用交替正则时勿加引号穿过 cmd。

**F2-R2 见 F1-R2（同一条，编号归并）。**

**F3-R2（P2，仓既有问题，非本轨引入）：`-R lab_pack` 命中的 #948 "pack manifests are in sync with gen_lab_packs.py (zero diff)" 在 Windows 宿主必然失败。** tests/test_lab_data_pack.cpp:593-605 用 `std::system("python --version > \"${TMPDIR:-/tmp}/...\" 2>&1")` —— `${TMPDIR:-/tmp}` 是 bash 语法，cmd 下为字面路径，重定向目标目录不存在 → 探测全败 → `REQUIRE(started)` 失败（实测两次复现）。该文件与本轨 diff 零交集，失败点在进程启动探测、与任何仓内容无关。residual owner：测试基础设施/pack 轨道（该门在 Windows 宿主需要 cmd 兼容 sink 或按平台跳过）。同组 #1179/#1241 等 `*_NOT_BUILT` 为部分构建树的既有占位，非失败语义。

**F4-R2（P3）：`test_lab_runtime` 的 "session store persists atomically and resumes deterministically" 用例在本宿主因 Windows 文件锁（remove_all "另一个程序正在使用此文件"）收尾失败** —— master 版 session_store.cpp 对照实验同样失败（24/25），与本轨 catch 改动无因果；疑似 Defender/索引器瞬时锁。residual owner：宿主/既有测试。

**F5-R2（P3）：`.planning/zcode-r23-D/tools/` 下非 cmd/sh 文件不被主动忽略**（无 `tools/*` + 反白名单），以 fail-loud 方式显式可见；与 r22-infra 既有块同构。可接受；若想收紧可加 `.planning/zcode-r23-D/tools/*` + 两条反白名单。

**F6-R2（P3，修饰性）：ed30c9790 提交信息有一处脱字**（"without it  selects zero cases"，反引号内容疑似被剥掉）。仅提交信息，不影响树。

**F7-R2（P3，正面记录）：修复提交对 R1 F2/F4/F5 的处置均恰当且经我独立验证**——措辞收窄后测试头与 derivation 不再读作 run_lab.cmd 端到端收口（且如实标注 E-track 一行改动）；cross-run oracle 以内容级断言替代跨目录 digest（路径入 digest body 的理由经 :2540-2543 亲读成立）；TempRootGuard 异常安全。

### 总体结论：**APPROVE**

- 代码：src/lab 两处 fail-closed 加固小而准；3 组对抗突变（深度炸弹 / sigma 窗口 / isfinite 门）全部按预期转红、全部还原、复绿 5/5——测试辨别力是实测的。
- 规则文件：对 validateRules 全项通过（权重和=100 精确、枚举合法、stem 一致），与 bundle overlay 逐参数 IDENTICAL，与 fixture 权威互斥不遮蔽，三级解析顺序亲读属实。
- 修复提交：TEST_PREFIX 落库且 `-R r23_d` 实选 5 例；cross-run 断言与 summary 扁平结构吻合；RAII 无 double-remove、异常安全；derivation 措辞修改后 JSON 合法、参数零变；wrapper PATH 经亲启验证必要且充分。
- 越界：26/26 白名单、0 删除、tests/CMakeLists.txt 与 .gitignore 对 BASE 纯 append；标题全 ASCII；并行/标签/参数惯例与仓一致。
- 残留（P2/P3）全部为本轨之外的既有问题或修饰项，已注明 residual owner，不阻塞合入。

—— R2 独立评审人，2026-10-27。本评审除本文件外未修改/提交任何文件；所有突变已还原（`git diff HEAD` 对三个被突变文件为 0 行，最终 `git status --porcelain` 仅本文件未跟踪）；未 push。
