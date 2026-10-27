# r23-D: LabSpec/Grader/FaultLab/Verification 科学教学证据闭环

## 基线与复核
- BASE = origin/master @ `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（fetch 后复核，与协议快照一致）
- 建档时 Open PR=0 / Issues=144 / 无兄弟 worktree；push 前重查记录于 PR_COLLISION_MATRIX.md
- 白名单机器校验 + R1/R2 独立复核：26 文件全部落在本轨白名单；tests/CMakeLists.txt 与 .gitignore 对 BASE 纯 append（0 删除行）；冲突标记 0

## 改动（全部位于 R23-D 白名单：src/lab, data/labs/grading, tests, .planning, .gitignore append）

1. **CT-6 收口（P1）** — `data/labs/grading/ndvi_landsat.rules.json`（新增）：repo 侧 foundry 场景校准评分权威。
   - 背景：R21-E CT-6——repo 文档化学生流程（run_lab.cmd：生成样本→NDVI 流水线→评分）在 master 上必然 45/100，因为唯一 repo 规则（ndvi_basics）派生自 128×128 EPSG:4326 提交 fixture 场景，而流程消费的是 foundry 256×256 EPSG:32648 场景。bundle 侧早有 overlay 校准（packaging/bundle/labs/grading-overlay），repo 侧一直缺对应权威。
   - 校准：与 bundle overlay 逐参数一致（R2 机器比对 IDENTICAL）；本机（Windows/GDAL 3.12.4）实测 foundry 工件 mean=0.452126129651 / sigma=0.508464660071 / nodata=131/65536，与 overlay 的 Linux 量测逐位吻合（foundry 确定性契约）。工件对新规则 = pass 100/0 扣分；对旧 fixture 规则 = 45/100 fail（CT-6 复现）。
   - **不放宽任何规则**：fixture 权威 ndvi_basics.rules.json 零改动（其钉值被 test_lab_grading 语料锁定）；两场景两权威，互斥守卫用例双向夹住漂移。
   - 范围说明：run_lab.cmd 的 `--lab ndvi_basics → ndvi_landsat` 一行切换属 E 轨（scripts/），见 CROSS_TRACK CT-D1。

2. **session_store 深度炸弹 typed 拒绝（P2/P3）** — `src/lab/session_store.cpp`：readJsonFile 补 try/catch(Json::Exception)。jsoncpp 1.9.6 的 stackLimit 超限是 throwRuntimeError（reader.h 明文 + json_reader.cpp:1040），此前 >64 层会话文件使 Json::RuntimeError 逃逸 LabSessionStore::loadAny 公共 API——违背同文件注释宣称的 typed rejection 与 lab_pack/grader_json/recipes 全仓 catch 纪律。

3. **spec_runtime expected_numeric 有限性门（P3）** — `src/lab/spec_runtime.cpp`：isfinite 拒绝 ±inf 边界（jsoncpp 把 1e999 字面量映射为 ±inf，[-inf,+inf] 会真空接受一切数值答案）；对齐 grader_types 纪律；data/labs 全目录扫描 0 个非有限字面量（无既有文档受影响）。

4. **新增测试** — `tests/test_r23_d_ct6_lab_flow.cpp`（3 用例）+ `tests/test_r23_d_lab_hardening.cpp`（2 用例）+ tests/CMakeLists.txt 末尾 append-only 注册块（含 TEST_PREFIX 轨道门，R1-P1 修复）。

5. `.planning/zcode-r23-D/**` 全套账本（BASELINE/TRIAGE/TASKS/ORACLES/DECISIONS/RISK/COLLISION/CROSS_TRACK/LEDGER/TEST_RESULTS/PERF_BASELINE/REVIEW_R1/REVIEW_R2/FINAL_REPORT/工具脚本）。

## RED → GREEN 证据
- RED（master 源码 + 新测试，规则文件移出）：`ctest -R r23_d` → **5/5 Failed（exit 8）**，含 `Exceeded stackLimit in readValue().` 异常逃逸实证与 `REQUIRE(!result.ok)→false`（±inf 被接受）
- GREEN×2（修复+规则就位）：**5/5 Passed（exit 0）×2 轮**；R1 修复后复验 ×2 再绿
- 对抗突变（辨别力证明）：R1+R2 共 7 组（去 catch / mean 钉值 / sigma 窗口 / severity 降级 / isfinite 禁用 等）全部按预期转红、全部还原、复绿

## Issue 分诊（详见 .planning/zcode-r23-D/ISSUE_TRIAGE.md）
- **LIVE（本 PR 修复）**：CT-6（R21-E 遗留，非 GitHub Issue）；session_store 深度炸弹（新发现）；spec_runtime ±inf（新发现）
- **FIXED_OPEN（只加回归证明，不重修，协议禁止）**：#1593 #1598 #1599 #1601 #1602 #1610 #1611 #1612 #1613 #1648（a1ffd5c66）#1653（51eb7d1a6）#1678 #1694
- **OUT_OF_SCOPE（→ C 轨）**：#1607

## 测试与限制
- 本地：Windows/MSVC 19.38/Ninja/Qt 6.8.0/GDAL 3.12.4，build-D 隔离（vcpkg 依赖 3GB 隔离复制、离线配置），-j2 默认（协议 RSS 条款内短暂 -j4）
- 回归矩阵 33 套逐 exe 退出码：**25 绿**（含新增 2 套）；**7 套存量 Windows 基线失败**（4 套做 BASE 源码对照复跑、失败签名逐位一致——lab_runtime teardown 文件锁 / faultlab :1759 / lab_data_pack :417+:603 / lab_chains :222；3 套环境签名明确——labspec QProcess 拉起失败 / recipe_equivalence CRLF / r16_a_grader MSVC 1MB 栈深 512 递归 SIGSEGV）；**1 套 Windows 不可编译**（r16_a_faultlab = unistd.h，Linux lane 专属）。全部非本 PR 引入（diff 0 交集 + BASE 对照）
- Linux/macOS 真机 NOT_EXECUTED（本机不可得；Windows offscreen 通过不等于 Linux 通过，如实标注）
- 性能：全部改动为 NO_MEASURABLE_BENEFIT 类，N/A 理由见 PERF_BASELINE.md

## Review 证据
- **R1（独立、新上下文）**：PROCEED-WITH-FIXES（0 P0 / 1 P1 / 2 P2 / 4 P3）→ 全部闭环于提交 ed30c9790（TEST_PREFIX 落库 / 措辞校准 / 收尾提交 / 跨跑 oracle / RAII 清理）。报告：.planning/zcode-r23-D/REVIEW_R1.md
- **R2（第二位、新上下文，独立重验 R1 全部关键声明）**：**APPROVE（0 P0 / 0 P1）**。3 组反例独立复现、R1 修复零回归、26/26 白名单、规则契约逐参数与 bundle overlay IDENTICAL、标题全 ASCII。报告：.planning/zcode-r23-D/REVIEW_R2.md

## 风险与回归保护
- 双权威互斥守卫（任一规则文件向对方场景漂移即红）+ 钉值消费断言（mean/sigma 突变即红）+ 深度炸弹/±inf 防护用例 + 既有修复回归矩阵（防 merge-loss）
- CROSS_TRACK：CT-D1（E 轨一行：run_lab.cmd `--lab ndvi_landsat`）；CT-D2（C 轨信息：#1648 curriculum_editor 部分已修）；CT-D3（双验证器归属记录）
- P3 残留（residual owner 已在 REVIEW_R1/R2 列出）：跨进程 digest 对照（F4 已以内容级等价替代）、测试失败路径 %TEMP%（F5 已 RAII 修复）、负控断言次序备注（F6，记录性）
