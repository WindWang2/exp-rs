# REVIEW_R2 — R25-C（第二个独立上下文复审）

- 评审对象：`git diff origin/master...HEAD`，worktree `/home/kevin/projects/rs-studio/exp-rs-r25-C-20261028T140339Z`，分支 `agent/r25-C-pi-mcp-execution-20261028T140339Z`（4 commits ahead of `2610cb4741`）
- 评审人上下文：未参与本轨实现；只读码 + 允许的小型 JS/既有二进制复跑。**未执行任何构建**（遵守约束）。
- 复审重点：R1 修复是否正确/完整、修复是否引入新缺陷、并行合并风险、测试判别力、契约诚实性。
- R1 报告仅作为背景输入；以下结论均由本上下文独立取证（含沙箱复现、Qt 探针、PCRE2 语义取证、object-file 取证）。

---

## 已执行的验证（命令 + 结果）

| # | 验证 | 命令 | 结果 |
|---|---|---|---|
| V1 | 全套件回归（含真二进制 E2E） | `cd pi && node --test "test/*.test.mjs"` | **46 pass / 0 fail / 0 skip**（wall 17.3s；E2E 与其他文件并行） |
| V2 | 单跑真二进制 E2E | `cd pi && node --test test/r25_c_e2e_real_host.test.mjs` | **pass，17.0s**（`build-dev/tests/surface_mcp_host` 存在 → 该链确实被执行，非 skip） |
| V3 | 新契约测试 | `cd pi && node --test test/r25_c_generation_drift.test.mjs` | 4/4 pass |
| V4 | C++ 新测试（用 worktree 内**既有**二进制，非新构建） | `build-dev/tests/test_r25_c_capability_honesty` / `test_r25_c_workflow_resume_diagnostics` | **198 assertions pass** / **72 assertions pass（2 cases）** |
| V5 | C++ 二进制确实由**修复后**源码编译 | `strings -el <mcp_server.cpp.o>` + `nm -C` | object 内含 `supportsCancellation`(UTF-16)、`^[A-Za-z0-9_.-]+\Z`、符号 `diagnosticArtifactPath`、`handleGetWorkflowStatusByRun`、未定义引用 `runForRunIdReadOnly`/`hasCheckpointFor` → **R1 修复的 C++ 侧已被编译并测试** |
| V6 | R1 修复判别力（沙箱全量回退 5 处修复） | `/tmp/r2red`（复制 pi/，把 `killer.unref()`、`killNow`、onGeneration 异步兜底、错误类透传、`reListInFlight` 全部改回 master 形态）后 `node --test "test/*.test.mjs"` | **45 pass / 0 fail / 1 skip（仅 E2E 因沙箱无 build-dev 而 skip）→ 5 处修复全部回退后套件仍全绿** |
| V7 | `killNow()` 可达性（精确复刻 exit hook 两段循环） | `/tmp/r2check/hook3.mjs` | `killNow iterations: 0`，`activeBridges` 在 stop() 循环后为 **0**，`b.child` 已为 `null`；SIGTERM-忽略子进程**存活** |
| V8 | 交换循环顺序是否修复孤儿 | `/tmp/r2check/swap.mjs`（`killNow()` 先于 `stop()`） | 子进程被 SIGKILL，**不再存活** → 修复方案一行可验证 |
| V9 | SIGKILL escalation timer（去掉 unref）是否生效 | `/tmp/r2check/probe6.mjs`（仅 `stop()`，等 7s） | 子进程在 ~5s 死亡 → **该半修复有效** |
| V10 | onGeneration 两种失败模式 | `/tmp/r2check/ongen2.mjs` / `ongen3.mjs`（真 fake server，回调分别返回 rejected promise / 同步 throw） | 均被兜住并打日志，`start()` 正常 resolve，无 unhandledRejection → **R1 P2-1 修复功能正确** |
| V11 | `diagnosticArtifactPath` 语义（真实 Qt6 编译小程序，非项目构建） | `/tmp/r2qt/t.cpp` + `g++ $(pkg-config Qt6Core)` | root=`/` 时**所有**绝对路径塌成 basename；root=`/tmp` 时正常；不存在的含 `..` 路径被原样放行 |
| V12 | PCRE2 `\Z` vs `\z` 语义 | `printf '/^[A-Za-z0-9_.-]+\\Z/\n    run\\n\n' \| pcre2test`；`man pcre2pattern` | `\Z` **匹配** `run\n`（"also matches before a newline at the end"）；`\z` 不匹配 |
| V13 | snapshot 与 C++ 表一致性 | python 比对 `tests/surface_diff_snapshot.json` ↔ `meta_protocol_tools.cpp` 字面量 | 描述串、属性序、`required` 省略**逐字符一致** |
| V14 | CMakeLists 仅末尾追加完整性 | `python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` | `1065 targets + 50 add_test`，`lost: 0+0; added: 2+0`，0 marker finding |
| V15 | .gitignore 否定规则是否真生效 | `git check-ignore -v` + `git ls-files` | 未被忽略，12 个 md 已跟踪 |
| V16 | 轨道边界 | `git diff origin/master...HEAD --name-only` | 未触碰 `src/{app,ui,operators,core,gui}/`、`src/agent/{cartography,mapspec,layout_tools,symbology}/` |

---

## R1 修复逐项结论

| R1 finding | 修复位置 | R2 判定 |
|---|---|---|
| P2-1 onGeneration 异步拒绝杀进程 | `pi/mcp_bridge.ts:409-426` | **正确**（V10 实证两种模式都兜住）。缺测试。 |
| P2-2 "none detected" 声称未完成的比对 | `pi/exp-rs-spatial.ts:59,126,140,152,306` | **单次在飞场景正确**；重叠在飞 + 陈旧 added/removed/lastError 仍会被当作"当前"结论呈现（见 F5）。缺测试。 |
| P3-3 respawn 重打错误类 | `pi/mcp_bridge.ts:605-613` | **正确**（透传 `bridgeError ?? RESTART_FAILED`）。缺针对性测试（测试接受 3 个码）。 |
| P3-4 terminateChild SIGKILL 不触发 | `pi/mcp_bridge.ts:482-484` | **半对**：去掉 `unref()` 有效（V9）；但 commit 宣称的 "process-exit hook 经 killNow() 立即 SIGKILL" **完全不成立**（V7）——见 F1。 |
| P3-2 output_path 披露 | `src/agent/mcp_server.cpp:3131-3151` | **不完整**：root=`/` 时全部塌成 basename（V11），且零测试覆盖（V4 的用例从不设置 `outputLayerPath`）——见 F2。 |
| P3-2b checkpoint_present 诚实探测 | `src/agent/mcp_server.cpp:3193` + `workflow_run_coordinator.cpp:1819-1823` | **正确**（`hasCheckpointFor` 单锁、不嵌套、无 flock）。无 `false` 分支测试。 |
| P3-1 supportsCancellation | `src/agent/mcp_server.cpp:1596-1601` | **descriptor 路径正确**（V4 实证 `gdal:reproject`  guarantees 为 3 个 bool）；但 `providerEntryMap` 只有 2 键，与 commit "provider entries carry the same shape" 不符——见 F4。 |
| P3-6 run_id 正则锚定 | `src/agent/mcp_server.cpp:3158`、`:3284` | **未闭环且注释为假**：`\Z` 在 PCRE2 中同样匹配末尾换行（V12），`"run\n"` 仍可通过——见 F3。 |
| P3-5 死分支 | 已删 | 正确。 |
| P3-7 E2E kill→resume 竞态 | `pi/test/r25_c_e2e_real_host.test.mjs:188-243` | **修复有效且真跑过**（V2 pass）。但 commit message 描述的成因（"stop() was never called"）与事实不符——见 F7。 |
| P3-8 测试补齐 | `tests/test_r25_c_workflow_resume_diagnostics.cpp:254-340` | pipeline 路径 advisory 已覆盖（V4 pass）。其余缺口仍在（见 F6）。 |

**R1 的 P0/P1 = 0 判定我认同**：锁纪律、run_id 守卫位置、resumable advisory 的状态墙镜像、server 错误不重打、snapshot 一致性、CMakeLists 追加完整性均复核通过。

### 我独立复核为"正确"的部分（非 R1 复述）
- `runForRunIdReadOnly` 的 `shared_ptr` 在 `m_mutex` 内拷贝后返回，`loadCheckpoint` 在锁外；我进一步核实 `workflow_checkpoint.cpp` 的 `loadCheckpoint` **不取 flock**（只有 `:590` 的恢复扫描才 `WorkflowRunLock::tryAcquire`），因此只读诊断不会被活主的 flock 阻塞——R1 此条我追加了证据。
- `diagnosticArtifactPath` 的"非路径 token 原样返回"与 `run_workflow` 提交门（`absolutePathOutsideWorkspace` → `weaklyCanonicalPath`，`..` 后缺组件即 fail-closed 拒绝）叠加后，`..` 逃逸路径实际不可达；真正的问题是 root=`/`（见 F2），不是穿越。
- V5 的 object-file 取证推翻了"C++ 修复未编译"的怀疑：`src/agent/mcp_server.cpp` mtime 21:33:07 早于 fix commit 23:11:04，但所有相关 `.o`（21:34:30 / 21:34:50 / 23:09:13）都晚于它且含修复后字面量/符号，故 **fix commit 的 C++ 回归声明成立**（V4 也实证 198+72 全绿）。

---

## Findings

### P2-1 `killNow()` 是死代码：exit hook 的"立即 SIGKILL"从未发生
- 文件：`pi/mcp_bridge.ts:151-154`（hook）、`:717-735`（stop）、`:737-748`（killNow）
- 问题：exit hook 写成 `for (…) bridge.stop(); for (…) bridge.killNow();`。`stop()` 结尾无条件 `this.child = null`（`:734`）并 `activeBridges.delete(this)`（`:733`）。于是第二段循环遍历的是**空集合**（V7 实测 `killNow iterations: 0`），且即使集合非空，`killNow` 也会因 `this.exited`（`:719` 置位）与 `this.child === null` 立即返回。**两条独立原因都使它不可达。**
- 后果：Pi 进程退出时，一个忽略 SIGTERM 的子进程仍然成为孤儿并持有 workflow run 的 flock——正是 R1 报告的场景，**未被修复**。V7 实测该子进程在 hook 序列后依然存活。代码注释（`:142-150`）与 commit message 都宣称"the process-exit hook escalates to SIGKILL immediately via killNow()"，这是代码不支持的断言。
- 复现：`node /tmp/r2check/hook3.mjs`（精确复刻两段循环 + SIGTERM-ignoring 子进程）
- 修复：交换两段循环顺序即可（V8 实测有效）：
  ```ts
  process.on("exit", () => {
    for (const bridge of activeBridges) bridge.killNow();   // 先杀：this.child/exited 尚未被清
    for (const bridge of activeBridges) bridge.stop();
  });
  ```
  或在 `stop()` 里把 child 存到字段供 `killNow` 使用。建议同时补一条测试：SIGTERM-ignoring 假 server + 复刻 exit hook → 断言子进程死亡。

### P2-2 `diagnosticArtifactPath` 在 workspace root 为 `/` 时把所有绝对路径塌成 basename
- 文件：`src/agent/mcp_server.cpp:3142-3145`
- 问题：`const QString root = QDir( wsRoot ).canonicalPath();` 对 `/` 得到 `"/"`，随后判定 `resolved == root || resolved.startsWith( root + '/' )` 变成 `resolved.startsWith("//")`——**任何绝对路径都不以 `//` 开头**，因此 root=`/` 时所有 `output_path` 一律只剩文件名。`SICNU_MCP_WORKSPACE=/` 是 `workspace_containment.h:548-550` 明确记录的"把沙箱放宽到整个文件系统"的受支持用法，且本轨自己的 fixture `tests/test_r25_c_capability_honesty.cpp:38` 就设成 `QDir::rootPath()`。
- 后果：W3 诊断面在受支持配置下静默退化（步骤输出只剩文件名），而 `:3126-3130` 的注释声称"Paths inside the root pass through verbatim"。fail-closed 方向，不是披露问题，但是功能性与契约双重缺陷。
- 复现（V11，真实 Qt6 小程序）：root=`/` → `/etc/passwd -> basename`、`/tmp/x.tif -> basename`；root=`/tmp` → `/tmp/x.tif -> VERBATIM`
- 修复：改用仓库自带的 `canonicalPathWithin( resolved, root )`（`workspace_containment.h:148-157`，已正确处理尾斜杠 root 与大小写），不要再手写前缀比较：
  ```cpp
  const QString root = sicnu::agent::tool_catalog::containment::canonicalWorkspaceRoot( wsRoot );
  if ( !root.isEmpty() && containment::canonicalPathWithin( resolved, root ) ) return rawPath;
  ```
  并补一条 fixture 为 `/` 的用例断言 `output_path` 原样返回（当前 `seedCheckpoint` 从不写 `outputLayerPath`，该函数零覆盖）。

### P2-3 R1 的 5 处修复没有任何一条具备判别力的测试（全量回退后套件仍全绿）
- 文件：`pi/test/r25_c_generation_drift.test.mjs`、`tests/test_r25_c_workflow_resume_diagnostics.cpp`
- 问题：V6 把 5 处修复（`killer.unref()`、`killNow`+第二段循环、onGeneration 异步兜底、错误类透传、`reListInFlight`）在沙箱里全部回退成 master 形态，跑全套件 → **45 pass / 0 fail**（唯一 skip 是沙箱无 `build-dev` 的 E2E）。也就是说：把 R1 的修复全部删掉，没有任何测试会红。F1 正是在这个盲区里漏过去的。
- 复现：`rm -rf /tmp/r2red && cp -r pi /tmp/r2red/pi`（按 V6 脚本回退）后 `cd /tmp/r2red/pi && node --test "test/*.test.mjs"`
- 修复：至少补 3 条：(a) exit-hook 顺序/SIGKILL 孤儿（覆盖 F1）；(b) `onGeneration` 返回 rejected promise 时 `start()` 仍 resolve 且无 unhandledRejection（V10 脚本可直接改成断言）；(c) `reListInFlight` 在飞时 `exprs_status` 输出 "comparison still in flight" 而非 "none detected"（需要把 status 渲染逻辑抽成可导入函数，或补一个最小 ExtensionAPI mock）。

### P3-1 `\Z` 并没有比 `$` 更严格：trailing-newline run id 仍被接受，而注释宣称已修复
- 文件：`src/agent/mcp_server.cpp:3156-3158`、`:3282-3284`
- 问题：PCRE2（Qt `QRegularExpression` 的引擎）中 `\Z` 的语义是 "matches at the end of the subject **also matches before a newline at the end of the subject**"，`\z` 才是绝对末尾（V12：`pcre2test` 对 subject `run\n`，`\Z` 命中、`\z` 不命中；`man pcre2pattern` 同述）。因此把 `$` 换成 `\Z` **没有**堵住 `"run\n"`，注释 "\Z anchors end-of-string" 与 R1 的修复意图都是假的。新路径 `handleGetWorkflowStatusByRun` 与预存的 `handleResumeWorkflow` 同样受影响。
- 影响评估（与 R1 P3-6 一致，故定 P3）：MCP 入口处 `run_id` 已 `.trimmed()`（`:1097`），且字符类排除 `/`、`\` 并显式拒 `..`，无目录穿越后果；仅直接 C++ 调用可达。但这是"守卫注释断言了一个它不具备的属性"，且测试（`tests/test_r25_c_workflow_resume_diagnostics.cpp:231-241`）只覆盖 `../escape`/`a/b`/`run..x`，不含 `"run\n"`。
- 复现：`printf '/^[A-Za-z0-9_.-]+\\Z/\n    run\\n\n' | pcre2test`
- 修复：用 `\\z`（小写）；并加一条 `runArgs("run\n")` → `INVALID_PARAMETER` 的用例钉住。

### P3-2 `reListInFlight` 单布尔无法表达"多次在飞"，且陈旧漂移/错误会被当作当前结论
- 文件：`pi/exp-rs-spatial.ts:59,126,140,152,301-309`
- 问题：(a) 代际 2 与代际 3 的 re-list 重叠时，先落地的那次把 `reListInFlight` 置 `false`，另一次仍在飞 → 状态回到 "none detected"；(b) 只有 `parts` 为空时才追加 "comparison still in flight"，因此**在飞期间上一次代的 `added/removed`/`lastError` 仍以"当前"结论呈现**——R1 P2-2 的后半句（"成功路径会把上一次代的 added/removed 原样留着呈现为当前"）只被修掉了一半。
- 复现：构造 `onGeneration` 连续两次触发且第一次 `tools/list` 延迟（沙箱可用 `--version-file` + 慢响应复现）；静态阅读亦可确认。
- 修复：把布尔换成 `pendingGenerations:Set<number>`（或 `inFlightGeneration` + `checkedAtGeneration`），在飞时无条件输出 "comparison still in flight (generation N)"，落地后才渲染 `added/removed/lastError`，并把三者绑定到 `checkedAtGeneration`。

### P3-3 `providerEntryMap` 的 `guarantees` 与 descriptor 路径不同形，且缺 `supportsCancellation`
- 文件：`src/agent/mcp_server.cpp:1652-1658`
- 问题：descriptor 路径发 3 个 bool（`deterministic`/`idempotent`/`supportsCancellation`，`:1596-1601`），provider 回退路径只发 2 个且值为 `QVariant()`（JSON `null`），**没有 `supportsCancellation` 键**。fix commit message 声称 "provider entries carry the same shape"，`tests/test_r25_c_capability_honesty.cpp:161-167` 的注释也声称 "both listing paths speak ONE shape"——两者都不成立。V4 实证该测试之所以绿，是因为 `gdal:reproject` 实际走的是 descriptor 路径（`AlgorithmEngine::registerProvider` → `discoverAlgorithms` 把 `ProviderAlgorithmAdapter` 注册进 `AtomicAlgorithmRegistry`，`handleListAlgorithms` 的 `seenIds` 于是命中），`providerEntryMap` 在本配置下几乎不可达；一旦有 provider 绕过 `AlgorithmEngine` 直接进 QGIS registry（GUI/插件常见），两个发现面就会对同一算法给出不同形状与不同真值（`true` vs `null`）。
- 复现：`grep -n providerGuarantees src/agent/mcp_server.cpp`（对比 `:1596-1601`）
- 修复：补齐 `supportsCancellation` 键，或明确在 DECISIONS 记录"provider 回退路径仅 2 键"以闭环 oracle 偏差。

### P3-4 同一工具两条路径的 `steps[].status` 词表不一致
- 文件：`src/agent/mcp_server.cpp:3116`（pipeline 路径，经 `mcpStatusForTask` → 小写 `running/completed/failed/canceled`）vs `:3236`（run_id 路径，`plan.status` → 大写 `Pending/Ready/Running/Completed/Failed/Canceled/Skipped`）
- 问题：同名键 `status` 在 `get_workflow_status` 的两条分支里是两套词表；E2E 自己在 `:179` 用小写判断 pipeline 路径。agent 必须同时处理两种拼写，而 schema/description 只说了 "per-step plan statuses"。
- 复现：`grep -n 'status' src/agent/mcp_server.cpp | sed -n '…'`（`:3116` vs `:3236`）；`mcpStatusForTask` 定义在 `:369-403`
- 修复：run_id 路径输出前做一次小写归一（或改键名为 `plan_status` 并在 description 中说明），并加一条断言钉住。

### P3-5 `output_path`/`checkpoint_present=false` 两个修复点零测试覆盖
- 文件：`tests/test_r25_c_workflow_resume_diagnostics.cpp:160-170`（`seedCheckpoint` 从不写 `plan.outputLayerPath`）、`:189-251`（所有 SECTION 都 seed 了 checkpoint）
- 问题：R1 P3-2 的两个修复（`diagnosticArtifactPath` 的 containment 判定、`hasCheckpointFor` 的诚实探测）都没有任何断言：没有任何用例让 `output_path` 出现，也没有用例断言 `checkpoint_present == false`。F2 的 root=`/`  bug 正是因此漏过。
- 复现：`grep -c outputLayerPath tests/test_r25_c_workflow_resume_diagnostics.cpp` → 0
- 修复：给 `seedCheckpoint` 加一个可选的 output path 参数，补 (a) 工作区内原样、(b) 工作区外塌成 basename、(c) root=`/` 原样三条；再补一条内存内 run 无 checkpoint 时 `checkpoint_present == false`。

### P3-6 E2E 不断言 `resumable` 的值
- 文件：`pi/test/r25_c_e2e_real_host.test.mjs:222`
- 问题：`assert.equal(typeof d.resumable, "boolean")` 对 true/false 都通过。被杀 mid-run 的 run 其 checkpoint 状态大概率是 `Running` → `resumable=false`，所以这条断言**结构上无法发现 advisory 逻辑整体坏掉**（例如被硬编码成 `true` 也照样绿）。
- 复现：把 `resumable` 在沙箱副本里硬编码 `true`，E2E 仍 pass
- 修复：对已知状态断言具体值（或至少断言 `resumable === (run_state ∈ {Interrupted,Failed,Canceled})` 与 `resume_hint` 文案匹配）。

### P3-7 文档/commit message 与事实不符（3 处）
- 文件：`41425da645` 的 commit message；`.planning/zcode-r25-C/LOOP_LEDGER.md:21`（第4轮 "更新 TEST_RESULTS"）；`.planning/zcode-r25-C/TEST_RESULTS.md:14-22`
- 问题：
  1. commit message 称 "stop() was never called mid-test — the host stayed alive holding the lock"，但 `8841094002:pi/test/r25_c_e2e_real_host.test.mjs:188` 已有 `bridge.stop()`；本次真正新增的是 `await sleep(6000)`、checkpoint 持久化轮询与有界 resume 重试。描述错了成因（修复本身有效，V2 实证）。
  2. LOOP_LEDGER 第4轮声称 "更新 TEST_RESULTS"，但 `git log --name-only` 显示 `41425da645` 只改了 `REVIEW_R1.md`，TEST_RESULTS.md 自 `a90bb64691` 后未动。（补充：第3/4轮两行 ledger 本身还是**未提交的工作区编辑**——`git status` 显示 `LOOP_LEDGER.md` 为 M，mtime 23:11:12 晚于 commit 23:11:04——所以这两条"全绿/已更新"的声称甚至不在分支历史里。）
  3. TEST_RESULTS.md 仍把 C++ 测试与 E2E 列为 "待构建完成后运行"，而 V1/V2/V4 实证二者均已执行并通过（pi 46/46 含 E2E；198+72 断言全绿）。
- 复现：`git show 8841094002:pi/test/r25_c_e2e_real_host.test.mjs | sed -n '185,190p'`；`git log --oneline --name-only origin/master..HEAD -- .planning/zcode-r25-C/`
- 修复：更新 TEST_RESULTS.md 记录 V1/V2/V4 的真实结果，并修正 commit/ledger 的成因描述。

---

## 并行合并风险（c）

| 项 | 结论 |
|---|---|
| `tests/CMakeLists.txt` | **低风险**。仅末尾追加，`# BEGIN R25-C`/`# END R25-C` 标记完整，未重排/删除他人块；仓库自带 `scripts/check_merge_registration_integrity.py` 复核通过（V14：+2 targets，lost 0，0 marker finding）。唯一残余风险是各轨都在 EOF 追加导致的文本冲突，标记块可机械解决。 |
| `.gitignore` | **低风险**。末尾追加本轨 allow-block，与 r14-C/cli-surface-r4 等既有逐轨块同形；V15 实证否定规则生效、md 已跟踪。 |
| `tests/surface_diff_snapshot.json` | **一致**。V13 逐字符比对：description、`pipeline_id`/`run_id` 属性序、`required` 移除（两个输入都 `required=false` → `schema()` 不再发 `required` 键）与 `meta_protocol_tools.cpp` 完全吻合。风险仍是"他轨也改 snapshot"——PR_COLLISION_MATRIX 已登记"push 前检查新增 PR 是否也改，冲突则再再生保并集"，该协议正确但**尚未执行**（当前 0 open PR）。 |
| `src/agent/mcp_server.*`、`src/workflow/workflow_run_coordinator.*`、`pi/*` | 单写者，矩阵已登记；`workflow_run_coordinator` 仅新增 2 个只读方法，未改既有签名/行为，合并冲突面小。 |

---

## 测试判别力（d）

- **原始特性测试（W1/W2/W3）判别力成立**：R1 已在 master 源码上实证 4/4 RED；我未重复，但 V3/V4 确认 GREEN，且 V5 确认 C++ 二进制含修复后符号。
- **R1 修复本身的判别力为 0**：V6 全量回退 5 处修复后套件仍 45 pass / 0 fail。这是本次复审最实质的测试缺陷（见 P2-3）。
- **弱断言清单**：E2E `resumable` 只查类型（P3-6）；`RESTART_FAILED` 用例接受 3 个错误码（`r25_c_generation_drift.test.mjs:245-248`），因此 P3-3 修复回退不会被发现；`test_r25_c_capability_honesty.cpp:168-173` 的 provider 段在 host 无 `gdal:reproject` 时 `WARN + return` 静默通过（当前 host 有该条目，7 断言实跑，但这是一个环境依赖的 vacuous-pass 形状）。

---

## 契约诚实性（e）

1. **F1（P2）**：`killNow` 相关的代码注释与 commit message 断言了一个不存在的进程退出行为。
2. **P3-1（P3）**：run_id 守卫注释断言 "\Z anchors end-of-string"，PCRE2 语义不支持。
3. **P3-3（P3）**：fix commit "provider entries carry the same shape" 与测试注释 "both listing paths speak ONE shape" 均不被代码支持。
4. **P2-2（P2）**：`diagnosticArtifactPath` 注释 "Paths inside the root pass through verbatim" 在 root=`/` 时不成立。
5. **P3-7（P3）**：E2E 修复成因、TEST_RESULTS 状态、LOOP_LEDGER "更新 TEST_RESULTS" 三处与事实不符。
6. **确认诚实的部分**：`resumable` 对 active 状态不谎报 true（`:3196-3204` 明确把所有权裁决留给 resumeRun）；`source: checkpoint` 标注为观察而非恢复；DECISIONS #2（server 错误不重打）与 `onLine:562-568` 实现一致；`generationsValue` 只在握手成功后自增（`:382`）与注释一致。

---

## 汇总

- P0：0
- P1：0
- P2：3（F1 killNow 死代码 / F2 root=`/` 路径塌缩 / F3 R1 修复零判别力测试）
- P3：7

无阻塞级正确性或安全缺陷。三个 P2 都是小范围、局部化修复，其中 F1 有一行级修复且我已实证有效（V8），F2 可改用仓库既有 `canonicalPathWithin`，F3 需要补 3 条测试。F1/F2/F3 都落在本轨的核心主张（崩溃后诚实、可诊断、可恢复）上，且 F1/F2 都是 R1 修复自身引入或未闭环的问题，建议合并前闭环；P3 可作后续清理（P3-1 的 `\z` 与 P3-7 的文档更正建议同批处理，成本极低）。

R2_VERDICT: NEEDS_FIXES（P0=0，P1=0，P2=3）
