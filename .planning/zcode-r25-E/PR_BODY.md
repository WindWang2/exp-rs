# r25-E: 本科遥感实验、LabSpec评分反馈与科学验证闭环深化

**BASE SHA**: `2610cb4741e69a89124921e44c03e340e2522ee2`（origin/master，2026-10-27 16:49 UTC merge #1723；push 前复查未漂移）
**HEAD SHA**: `ec777d0e9a5fcc5c0887488ae8456f52da18467a`
**分支**: `agent/r25-E-teaching-grading-verification-20261028T170411Z`（16 commits）
**Worktree**: `exp-rs-r25-E-20261028T170411Z`（从 origin/master 新建，源仓未动）

## 交付（全部本地实跑验证，真实生产代码变更）

### 1. 教学管理 UX 深化 — CT-C6 协作取消 + 单扫描（#1695-17c 残留、r24-c CT-C6 移交）
- `src/teaching_admin/batch_assessment.{h,cpp}`：`discoverSubmissions` 新增取消感知重载（三个遍历 pass 每条目前观察取消标志；取消⇒返回空 items+空 skips，绝不返回部分列表——#1466 vanish 纪律）；`BatchAssessmentConfig::preDiscovered/preDiscoveredSkips` seam（调用方预览扫描即编排器的发现，**每批目录只走一次**，网络盘流量减半）；seam 是 transport 不进 `batchConfigDigest`（restart 仍能跨路径采用 checkpoint）。
- `src/app/teaching_admin/teaching_admin_dock.cpp`：预览扫描传 `&m_batchCancel`，结果经 seam 复用；取消发生在发现阶段⇒typed 快速失败（不发布空成绩册，"全班没交作业"误读消除）；关闭 dock 的析构 join 从"整目录扫描"收敛到"一次目录列表+一条目"。
- R2 P2 闭环：**产出零耐久行的运行绝不写 checkpoint**（此前会把既有可采用的 checkpoint 清空为 `{rows:[]}`，含"目录瞬时缺失"和"取消于指纹流期间"两扇门）；dock 取消消息按 rows 空否诚实分流。
- 互斥规则：master 已验证 #1695-3 主缺陷已修（r24-c），本轨只收残留，未重做。

### 2. LabSpec/步骤操作深化 — #1695-17d 空白作答（record+reload 双边界）
- `src/lab/session_state.cpp`：`recordAnswer` FreeText 拒空白作答；`sessionFromJson` 同拒（手改/损坏文档不能把空白答案偷渡回来——#1678 纪律的同一模式）；**Unicode 空格感知**（U+3000 全角空格、U+00A0 NBSP 等 17 个码位按 UTF-8 字节匹配——中文班级真实场景）。
- `src/lab/spec_runtime.cpp`：spec 验证器拒空白 declared choice（record/reload 写读不变式）；`isBlankAnswerText` 提升为**单一共享定义**（session_state.h，三边界不漂移——#1653 漂移教训）。

### 3. 评分/科学验证可解释化 — 行内既有权威证据上桌
- `src/teaching_admin/feedback_pack.cpp`：反馈包新增 `grading_evidence{top_deduction, grader_digest, unavailable_reason}`——学生看到可审计的"为什么扣分"，unavailable 行自解释为"可重跑"而非失败。全部取自 grader 实际产出的 row 字段，**不捏造**（缺证据保持可见空值）。
- `src/teaching_admin/class_summary.cpp`：班级摘要新增 `run_integrity{resumed_rows, checkpoint_invalidated, truncated_rows, skipped_files, cancelled_early}`——教师的"查看异常"透镜（R1 P1 曾发现生产路径打印假零：loader 丢弃 5 个报告级成员，已修复并回归）。
- `src/teaching_admin/batch_assessment.cpp`：`loadBatchReportFromFile` 补解析 5 个成员 + 敌对值校验（负数/非整数→0+warning，与 total 同纪律；坏 skip 条目=typed 拒绝）。

### 4. CT-D1 — repo 学生流程脚本指向正确权威（一行修复 + 契约守卫）
- `scripts/windows/run_lab.cmd`：第 3 步 `--lab ndvi_basics`（fixture 权威：128×128 EPSG:4326）改为 `--lab ndvi_landsat`（foundry 权威：256×256 EPSG:32648 seed 42）——该脚本步骤 1-2 产出的正是 foundry 场景，master 上必然 45/100（R21-E CT-6 实测、R23-D CT-D1 移交 E 轨）。已核验 repo 规则与 bundle overlay 逐参数一致。
- `tests/test_r25_e_repo_flow_contract.cpp`：脚本↔规则↔overlay 三方一致性守卫（改回 fixture 权威即红）。

## 测试（TDD：RED→GREEN×2 + 变异证明 + 对抗矩阵）

- **RED（master 代码+本轨测试）**：行为级 3 个 WP 全红——`ctest exit 8，5/8 failed`（空白作答 record/reload、脚本 fixture 权威、grading_evidence/run_integrity 缺失）；E1（取消+seam）为 compile-RED（master 无 3-arg discoverSubmissions 与 seam 字段）。
- **GREEN ×2**：`exit 0，112/112` 连续两轮（12 套：本轨 6 套 21 用例 + 10 套邻域回归 + 2 套重目标学生场景 test_r23_d_ct6_lab_flow/test_teaching_foundation_e2e）。
- **变异证明**：恢复 R2 修复前守卫 ⇒ 2 个新测试红 ⇒ 还原 ⇒ 绿。
- **对抗矩阵（≥4 类，全 fail-closed）**：深嵌套 checkpoint JSON（1200 层）、深嵌套发布报告、超大 checkpoint（65MiB>64MiB 上限）、截断垃圾 checkpoint、篡改行（外来 status 词汇+外来学生）、敌对报告级计数（负数/非整数/坏 skip 条目）。
- **Oracle #1 双场景**：学生 foundry 场景（生成→rs:spectral_index→ndvi_landsat 权威 PASS=100、双跑 digest 一致、错 CRS typed fail、双权威互斥守卫——经 r23-D 既有测试锁定，本轨补脚本契约守卫）；教师全环 E2E（导入班级→单扫描→批量评分→JSON+CSV 成对发布→fail-closed reload→反馈守卫+评分证据→摘要异常透镜→restart 采用）。
- 邻域回归零新增失败；`git diff --check` exit 0；merge 完整性门 exit 0（targets 1063→1069，lost 0，duplicates 0，markers 0）。

## Issue 归属
- **LIVE 修复**：CT-D1（r23-D 移交）、CT-C6（r24-c 移交）、#1695-17d、R2 新发现 checkpoint 清零门
- **回归保护**：#1694（checkpoint 权威身份/restart 采用）、#1689（cohort 泄露守卫跨新成员）、#1678（NaN/Inf+空白双边界）、#1652-1（引擎解析器一致性，既有覆盖）、#1695-3/#1695-5（不重做，行为回归）

## R1 / R2 双轮独立评审（均 fresh-context 子代理，R2 未读 R1 报告）
- R1：APPROVE-WITH-FIXES（1×P1 + 3×P2 + 6×P3）→ 全采纳（commit a8d38071eb）+ 全量复跑
- R2：APPROVE-WITH-FIXES（1×P2 + 4×P3）→ 全采纳（commit 201db42c6e + 测试提交）+ 变异证明闭环
- 详见 `.planning/zcode-r25-E/REVIEW_R1.md` / `REVIEW_R2.md`

## 跨轨风险与交接
- 零 A/B/C/D 轨所有权文件改动（仅 `src/lab`、`src/teaching_admin`、`src/verify_adapters` 无改动、`src/app/teaching_admin`、`scripts/windows/run_lab.cmd`、`tests/test_r25_e_*`、`tests/CMakeLists.txt` 尾部 append、`.gitignore` append）
- CT-E1（→C 轨，只读建议）：验证证据链 pixel-size 缺口（`GridInfo` 无 geotransform，规则 pixel_size_* 约束目前只在 grader 自身断言）——扩展需改 `src/agent/output_verifier.cpp`（C 所有权），E 侧文件已就绪；现状无漏检，仅记录。
- 见 `.planning/zcode-r25-E/CROSS_TRACK_FINDINGS.md`

## 性能
无可声称收益：取消检查 ~ns/条目；seam 使目录扫描 2→1 次（方向性收益，无墙钟断言——#1699 纪律）；其余为 O(len) 字符串扫描/小 JSON 成员。见 `PERF_BASELINE.md`。

## 未执行平台 / 限制
- Windows/MSVC 实机 cmd.exe 腿：无实机；run_lab.cmd 改动以脚本内容契约测试锁定 + 规则文件存在性守卫
- macOS：无实机
- 远端 CI：协议约定不等待
- 遗留行为记录：旧构建保存的"含空白自由作答"会话文件在新 reload 边界下 fail-closed 拒载（预期方向，fail-closed 而非静默接受）

## 本地命令与退出码
```
cmake -S . -B build-r25-E -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DFETCHCONTENT_SOURCE_DIR_PYBIND11=<复用缓存>   # exit 0
cmake --build build-r25-E --parallel 2 --target <16 targets>                                                          # exit 0
ctest --test-dir build-r25-E -j1 -R '<family regex>'                                                                  # exit 0, 112/112 ×2
git diff --check origin/master...HEAD                                                                                 # exit 0
python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs                  # exit 0
```
构建资源纪律：全程 `flock ~/.cache/exp-rs-build.lock` 串行 + `-j2`（共享宿主五轨并行），未用 `-j$(nproc)`，未杀兄弟进程。

不自动合并、不等待远端 CI。
