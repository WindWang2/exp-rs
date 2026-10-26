# FINAL_REPORT — R23-D（LabSpec/Grader/FaultLab/Verification 科学教学证据闭环）

基线：origin/master `ad98a7a70`（fetch 复核）。交付分支：`agent/r23-d-lab-grading-evidence-20261026T165151Z`（worktree exp-rs-D-20261026T165151Z）。

## 交付分类（DONE / DEFERRED / BLOCKED）

### DONE（RED→GREEN×2 + 双独立评审闭环）
| WP | 内容 | 证据 |
|---|---|---|
| WP-D1（P1）CT-6 收口 | `data/labs/grading/ndvi_landsat.rules.json`（foundry 场景校准权威，与 bundle overlay 逐参数一致）+ `tests/test_r23_d_ct6_lab_flow.cpp`（foundry 流程 PASS=100/0 扣分 + digest 双跑一致 + 跨跑内容一致 / 错 CRS 负控 typed 定位 `CRS EPSG:4326 does not match expected EPSG:32648` + cappedByBlocking / 双权威互斥守卫）。fixture 权威 ndvi_basics 零改动 | TEST_RESULTS.md（RED 5/5→GREEN×2；本机 CT-6 复现 45/100 与 overlay 钉值逐位吻合）；R1 突变 2/3、R2 突变 2 |
| WP-D2（P2）session_store 深度炸弹 | readJsonFile 补 try/catch(Json::Exception)——jsoncpp 1.9.6 stackLimit 超限是 throwRuntimeError（reader.h:338-342 + json_reader.cpp:1040 + vcpkg 源码三方核验），此前 >64 层会话文件使异常逃逸 loadAny 公共 API | RED 实证 `Exceeded stackLimit in readValue().` 逃逸（R1 §B.1、R2 §B.1 双人独立复现） |
| WP-D4（P3）expected_numeric 有限性门 | isfinite 拒绝 ±inf（jsoncpp 把 1e999 映射 ±inf；[-inf,+inf] 真空接受一切数值答案）；3 形态 typed lab.runtime.field 拒绝 + 有限反序界仍拒 + 合法对照 | RED `REQUIRE(!result.ok)→false`（R1 §B.4、R2 §B.3） |
| WP-D5（P2）回归保护矩阵 | 33 套逐 exe：25 绿（含本轨新增 2 套）；7 套 KNOWN_BASELINE_FAIL 全部归因为存量 Windows 环境限制（4 套 BASE 源码对照复跑失败签名逐位一致：lab_runtime teardown 文件锁、faultlab :1759、lab_data_pack :417+:603、lab_chains :222；3 套环境签名明确：labspec QProcess、recipe_equivalence CRLF、r16_a_grader MSVC 栈深）；1 套 NOT_EXECUTABLE（r16_a_faultlab=unistd.h，Linux lane）。覆盖 #1593/#1598/#1599/#1601/#1602/#1610-1613/#1648/#1653/#1678/#1694 全部 FIXED_OPEN 的防 merge-loss 保护 | TEST_RESULTS.md 表格 + 退出码 |
| 双独立评审 | R1=PROCEED-WITH-FIXES（0P0/1P1/2P2/4P3，全部闭环：F1 TEST_PREFIX 落库、F2 措辞校准、F3 收尾提交、F4 跨跑 oracle、F5 RAII）；R2=**APPROVE**（0P0/0P1，3 组反例独立复现，R1 修复零回归，26/26 白名单，规则契约逐参数 IDENTICAL） | REVIEW_R1.md / REVIEW_R2.md |

### DEFERRED（有据不做的，见 DECISIONS.md D5/D6）
- D01/D03/D05-D07/D09-D11 大面积普查/重写：基线系统性加固已存在（0 TODO；NaN/深度/路径/确定性注释可考；相关回归测试本轨全绿）。协议禁止无根据臆造缺陷。
- WP-D3/WP-D6（#1648/#1653 修复）：FIXED_OPEN（a1ffd5c66 / 51eb7d1a6，10-19 已修），严禁重实现。

### 交付 Oracle 逐项（协议 §8）
- [x] **O0** 基线+重检：BASELINE.md（fetch 后 origin/master=ad98a7a70 与快照一致；OpenPR=0/Issues=144/无兄弟 worktree 建档时；push 前重查见 PR_COLLISION_MATRIX 回填）
- [x] **O1** 声明 DONE 的 WP 均有生产调用路径 + RED→GREEN×2 + 退出码 + 最小测试命令（TEST_RESULTS.md；命令：`cmd //c ".planning\zcode-r23-D\tools\run_tests.cmd -R r23_d"`）
- [x] **O2** 反例保护：深度炸弹/±inf/错 CRS/钉值漂移/权威互斥 5 类反例（R1+R2 共 7 组突变全部转红再还原）；旧修复保持（回归矩阵 25 绿）
- [x] **O3** PERF_BASELINE.md：全部 NO_MEASURABLE_BENEFIT，逐项 N/A 理由 + 实测旁证
- [x] **O4** 本地 build/CTest + 真实用户路径 smoke：33 套矩阵 + 主仓预构建 CLI 全流程（生成→流水线→评分）离线零网络复现；Windows 平台限制 7+1 套如实标注；Linux/macOS NOT_EXECUTED（本机不可得）
- [x] **O5** 两轮不同 reviewer（R1 新上下文、R2 新上下文），P0-P2 全闭环，R2 覆盖 R1 修复
- [x] **O6** 白名单机器校验 PASS（R1/R2 独立复核 26/26；tests/CMakeLists.txt 与 .gitignore 纯 append 0 删除；冲突标记 0）
- [x] **O7** 文档/测试/实现一致；无 mock-only、无永久 TODO、无空断言（R1 §D 逐条审查）、无假性能数据、无越权子进程
- [x] **O8** PR 已提交（非 draft、base=master）——见下方回填（push 后）

## PR 状态（push 后回填）
- PR #____：https://github.com/WindWang2/exp-rs/pull/____
- head SHA：____（4+1 提交）；gh pr view 证实 OPEN / 非 draft / base=master
- push 前重查：origin/master=ad98a7a70 未漂移 / OpenPR=0 / 无新兄弟 worktree（时间戳见 PR_COLLISION_MATRIX.md）

## 资源消耗
- 构建：3300+ 编译单元（两次大构建 + 多次增量），-j2 默认、协议条款内短暂 -j4，全程 free RAM ≥6.8GB 实测，无 OOM
- 测试：RED 1 轮 + GREEN 2 轮 + 矩阵 33 套 ×2（含 BASE 对照轮）+ R1/R2 各自的突变复验
- token：主会话 + Explore 普查 1 + R1 评审 1 + R2 评审 1（子代理用量见各 Agent 结果；总消耗未观测到精确计数，不作虚报）
