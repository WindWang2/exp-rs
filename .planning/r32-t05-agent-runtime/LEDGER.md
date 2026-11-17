# LEDGER — R32-T05

## 检查点 2026-11-17T05:5xZ（本地验证批）

- 构建成功（BUILD_EXIT=0，0 编译错误，受影响目标 14 个 + 补充 5 个）
- CTest（-j1，offscreen，实测）：
  - `r32_t05_dock::` **4/4 PASS**
  - `r32_t05_ops::` **4/4 PASS**
  - `test_agent_tool_host` + `conversation_store::` + `test_agent_ops_core::` + `r27t03` → **55/55 PASS**
  - `test_exprs_plugin_system/loader` + `test_native_conversation_runtime` + `test_python_runtime_selection` → **100%（1 skipped=Windows probe）**
  - `Conversation metrics` + 其余 copilot_ui 用例 → PASS
  - `external process` → **8/8 PASS**
  - `test_agent_loop_core` 直跑 → **17 cases / 216 assertions 全过**（含 #2079）
  - `bilingual_ui::` 3 例：case2(approval)/case3(task center) PASS；case1 剩 4 断言失败（2×Qgs Base 上下文 + 2×zh"Send"）→ "Send"断言已修为 "Send ▶"（重建中）；zh Base 上下文归 T06（#2081 .ts 侧）
  - `pi_process`：**发现 Linux fixture 缺执行位**（write() 不 chmod+x；Windows CI 不需要）→ 已修（QFile::setPermissions）；重跑待 host lock（T04 全量构建占用）
  - copilot_ui toolcard 用例：**基线既有缺陷 = #2130**（r31-T02 构建交叉验证同样失败）→ 按其期望修 stub descriptor（producesFile 输出端口，重建中）
- 根因诊断记录：FailedToStart 复现链（t3/t4/t6 二分定位 Qt 写副本无 +x → EACCES）
- 已修测试 commit 待提交（pi_process exec bit、bilingual Send ▶、copilot_ui #2130）

## 检查点 2026-11-17T03:2xZ

- base：`53412c86bfd5742f0df0243b51500ba444f7dde1`（origin/master）
- HEAD：`580833562b`（5 个功能 commit，见下）
- 已完成（代码层，待本地构建验证）：
  1. `aab384a7d8` #2076/#2077/#2080（PiProcess 期限/竞态/脱敏 + JS 测试 4/4 PASS + test_pi_process 新用例 3 个）
  2. `afca5d2023` #2073/#2074/#2075（dock 生命周期 + hasProjectContext + test_r32_t05_agent_dock）
  3. `b87ddcd97a` #2078/#2079（审批单次/typed 校验 + test_r32_t05_ops_sdk）
  4. `d528ac3e36` #2109/#2111/#2112（install 规则/UTF-8 路径/大小写摘要）
  5. `580833562b` #2106/#2107/#2081-fixture（测试漂移）
- JS 验证（本机 node 24.21.0）：`checkpoint-redaction.test.mjs` 4/4 PASS；`protocol.test.mjs` 9 PASS/2 FAIL（installed-entry 两例需锁定 SDK stage——Linux 无 Windows 发行版，NOT_EXECUTED 维持）；`checkpoint/auth-context/controlled-session/checkpoint-protocol` 需 `SICNU_TEST_PI_NODE_MODULES`（同前）
- 构建：configure 重试循环运行中（host lock 与 T02/T06 轨争用，多次 exit 75 后等待重试）
- test_pi_process 本地运行方案：`/tmp/pi-stage-t05`（真实 vendor recipes + 宿主 node 副本，`SICNU_TEST_PI_NODE=/tmp/pi-stage-t05/libexec/sicnu-geo-rs/node/node.exe`）
- 未解决差距：C++ 编译/CTest 全部未跑；WP10-13/18-19 深化项未开始（按证据再选）；.ts 侧 #2081 归 T06
- 下一步：configure 完成 → 增量构建受影响目标 → ctest（r32_t05_dock/r32_t05_ops/test_pi_process/test_bilingual_delivery_ui/test_agent_copilot_ui/test_agent_tool_host 等）→ E2E → review

## 命令记录

- `python3 -I -B tests/test_r23_e_python_worker_typed.py` → **Ran 5 tests, OK**（2026-11-17；#2107 修复后全过。注：本机 AF_UNIX 可用，与研究快照的受限环境不同，原 3 FAIL 场景已动态复现为通过）
- `cmake --preset dev-default -DOpenCV_DIR=<共享scratch>/opencv/usr/lib/cmake/opencv5` → EXIT=0（与其他轨同惯例；初次无 OpenCV_DIR 报错后补）

- configure（重试循环）：
  `python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_HOST_LOCK" --parallel 4 --max-parallel 4 --wait 45 -- python3 scripts/dev/resource_guard.py --lock-dir "$EXPRS_BUILD" --parallel 4 --max-parallel 4 --wait 45 -- cmake --preset dev-default`
- JS：`node --test --test-concurrency=1 pi/product-runtime/test/checkpoint-redaction.test.mjs` → 4 pass / 0 fail（2026-11-17）
- 本地 stage 组装：`python3 -I -B /tmp/mk_pi_stage.py <wt> /tmp/pi-stage-t05` → recipes=7（与 `--print-recipe-digest` 一致）

## 检查点 2026-11-17T06:3xZ（rebase + master 前进）

- master 前进：53412c86 → 53c960a2（#2131 R31-T02 合入 + #2133 T06 合入）
- 重叠处置（rebase 冲突逐段解决）：
  - bilingual 测试与 copilot_ui #2130 修复：master #2133 为权威超集，整文件取 theirs（已记录理由）
  - tests/CMakeLists.txt：两侧尾部块都保留
  - dock/pi_process：函数级合并，8 个关键 TU 语法编译全过（双方修复共存验证）
- rebase 后 HEAD：548af38f61（8 commits on 53c960a2）
- 重建循环运行中（host lock 队列：T01→T03→…；40 次耐心重试）
- 待跑（重建后）：pi_process 全量（exec-bit 修复验证）、bilingual 3 例（.ts Base 上下文齐了应全过）、copilot_ui toolcard（#2130 master 版）、r31_t02_core（master 新增、受 pi_process 改动影响）、r32_t05 两套件复跑（绑最终 HEAD）

## 终验检查点 2026-11-17（HEAD 85f6347785）

- 重建完成（BUILD3_OK，0 错误）；host lock 抽签耗时 ~2h（六轨竞争，均按守卫协议排队）
- 终验全绿：pi_process 17/17（33.4s）、bilingual 3/3 + r31_t02_core 5/5、copilot_ui 9/9、r32_t05 8/8、复跑 67/67（20.4s）
- R2：SHIP + 增量 SHIP；rebase 回退缺陷（bilingual）自查发现并修复
- 下一步：推送 + PR（正文就绪于 .planning/r32-t05-agent-runtime/PR_BODY.md）
