# r23-D: LabSpec/Grader/FaultLab/Verification 科学教学证据闭环

> 状态：草稿（随进程回填证据；FINAL_REPORT 完成后定稿）

## 基线与复核
- BASE = origin/master @ `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（fetch 后复核）
- Open PR=0 / Issues=144 / 无兄弟 worktree（建档时）；push 前重查见 PR_COLLISION_MATRIX.md
- 白名单机器校验：`.planning/zcode-r23-D/tools/check_whitelist.sh origin/master` → PASS（回填输出）

## 改动（全部位于 R23-D 白名单）
1. **CT-6 收口（P1）**：`data/labs/grading/ndvi_landsat.rules.json`（新增，foundry 场景校准权威，与 offline bundle overlay 同校准方法/钉值；不动被测试语料钉死的 fixture 权威 ndvi_basics.rules.json）
   - 背景：R21-E CT-6——repo 文档化学生流程（生成样本→NDVI 流水线→评分）在 master 上必然 45/100（规则派生自另一场景）。bundle 侧已有 overlay 校准，repo 侧缺对应权威。
   - 证据：本机全流程预验证 foundry 工件 vs overlay 钉值 pass 100/0 扣分；vs repo fixture 规则 45/100 fail（复现 CT-6）。
2. **session_store 深度炸弹 typed 拒绝（P2/P3）**：`src/lab/session_store.cpp` readJsonFile 补 try/catch(Json::Exception)——jsoncpp 1.9.6 stackLimit 超限是 throwRuntimeError（reader.h:338, json_reader.cpp:1040），此前异常逃逸公共 API，违背同文件注释宣称的 typed rejection 与全仓 catch 纪律。
3. **spec_runtime expected_numeric 有限性门（P3）**：`src/lab/spec_runtime.cpp` isfinite 拒绝 ±inf 边界（jsoncpp 把 1e999 映射为 ±inf；[-inf,+inf] 会真空接受一切数值答案）；对齐 grader_types 纪律。
4. **新增测试**：`tests/test_r23_d_ct6_lab_flow.cpp`（3 用例：foundry 流程 PASS+digest 确定性 / 错 CRS 负控 typed 定位 / 双权威互斥守卫）、`tests/test_r23_d_lab_hardening.cpp`（深度炸弹 typed 拒绝 + ±inf 边界拒绝×3 + 合法对照）；tests/CMakeLists.txt 仅末尾 append-only 注册块。
5. `.planning/zcode-r23-D/**`：BASELINE/ISSUE_TRIAGE/TASKS/ORACLES/DECISIONS/RISK/PR_COLLISION/CROSS_TRACK/LOOP_LEDGER/TEST_RESULTS/PERF_BASELINE/REVIEW_R1/REVIEW_R2/FINAL_REPORT。

## Issue 分诊（详见 ISSUE_TRIAGE.md）
- LIVE：CT-6（R21-E 遗留，非 GitHub Issue）、session_store 深度炸弹（新发现 D-F1）、spec_runtime ±inf（新发现 D-F2）
- FIXED_OPEN（只加回归证明，不重修）：#1593 #1598 #1599 #1601 #1602 #1610 #1611 #1612 #1613 #1648 #1653 #1678 #1694
- OUT_OF_SCOPE（→ C 轨）：#1607

## RED → GREEN 证据（回填）
- RED：______
- GREEN×2：______

## 测试与限制（回填）
- 本地构建：Windows/MSVC/Ninja/Qt 6.8/GDAL 3.12.4，build-D 隔离，-j2/-j4 按协议
- ctest 全清单与退出码：见 TEST_RESULTS.md
- 未执行：Linux/macOS 真机（标记 NOT_EXECUTED；offscreen 语义在 Windows 验证）

## Review 证据（回填）
- R1：______（P0-P3 清单与闭环）
- R2：______

## 风险与回归保护
- 双权威互斥守卫用例防任一规则文件漂移；深度炸弹/±inf 用例防 merge-loss；既有修复回归矩阵（WP-D5）见 TEST_RESULTS.md
- CROSS_TRACK：run_lab.cmd（E 轨）一行 `--lab ndvi_landsat` 建议，见 CROSS_TRACK_FINDINGS.md CT-D1
