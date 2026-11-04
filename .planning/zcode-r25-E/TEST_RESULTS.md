# TEST_RESULTS — R25-E

环境：Linux x86_64（CachyOS LTS 6.18）、GCC（系统）、Qt6 系统包、CMake 4.4.4、Ninja/Makefiles、worktree build-r25-E（Debug, ENABLE_TESTS=ON，pybind11 源复用 r25-A 缓存）。构建 `-j2` + `flock ~/.cache/exp-rs-build.lock`（共享宿主五轨串行），ctest `-j1`、`QT_QPA_PLATFORM=offscreen`。

## 构建
- 全 16 目标（6 新 + 10 回归）build exit 0（/tmp/r25e-build5.log；中途两次测试代码笔误修正后重建）
- 重目标 test_r23_d_ct6_lab_flow + test_teaching_foundation_e2e（ Sicnu::agent+operators+foundry）：见下"重目标"

## RED（master 代码 + 本轨测试；`git checkout 2610cb4741 -- <4 文件>` 后重建直跑）
`ctest -R '^(test_r25_e_answer_hygiene|test_r25_e_feedback_evidence|test_r25_e_repo_flow_contract)'`
→ **exit 8，5/8 failed**（/tmp/r25e-red-run.log）：
- answer_hygiene.cpp:136 recordAnswer 接受空白（REQUIRE(!ok) 失败）+ :225 reload 接受空白
- repo_flow_contract.cpp:76 脚本第 3 步仍是 `--lab ndvi_basics`
- feedback_evidence.cpp:66/83/99/125 grading_evidence 与 run_integrity 块缺失
- E1（discovery cancel/seam）：**compile-RED**——master 无 3-arg discoverSubmissions 与 preDiscovered 字段，测试无法编译（新能力 TDD 的契约缺失形 RED；缺陷本身=r24-c 在码内自述的 close-time join 残留窗口）

## GREEN ×2（修复后；同命令）
- GREEN1: **exit 0，103/103**（/tmp/r25e-green1.log）
- GREEN2（无重编复跑）: **exit 0，103/103**（/tmp/r25e-green2.log）
覆盖：本轨 6 套（answer_hygiene 3、discovery_cancel 4、repo_flow_contract 2、feedback_evidence 3、teacher_e2e 1、adversarial_checkpoint 5 = 18 用例）+ 回归 10 套（test_lab_runtime、test_teaching_admin_core【含去空洞化的 mid-flight cancel 段】、test_teaching_batch_discovery_r10、test_r15_E_batch_refusal_accounting、test_r18_e_batch_checkpoint、test_r22_teaching_guard_hygiene、test_r22_teaching_batch_regrade、test_r23_c_teaching_pair_publish、test_teaching_admin_rubric_weights_r16_c_t06、test_teaching_admin_dock_smoke）
- RED→还原→GREEN 终验：exit 0，18/18（/tmp/r25e-final.log）

## 门禁
- `git diff --check origin/master...HEAD` → exit 0
- `python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` → exit 0；targets 1063→1069（+6），add_test 50→50，lost 0，duplicates 0，markers 0

## R1 修复后回归（commit a8d38071eb 起）
- 增量重建 exit 0（/tmp/r25e-build8.log）
- 全量套件（12 套，含重目标 test_r23_d_ct6_lab_flow + test_teaching_foundation_e2e）:
  - GREEN3: **exit 0，109/109**（/tmp/r25e-green3.log）
  - GREEN4（复跑）: **exit 0，109/109**（/tmp/r25e-green4.log）
- R1 新增/强化断言（checkpoint 保全三段式、空类不取消对照、blank-choice spec 拒绝、泄露种植进 grading_evidence、原子计数）全部绿

## 重目标学生场景（Oracle #1 场景一）
- test_r23_d_ct6_lab_flow（foundry 生成→rs:spectral_index→ndvi_landsat 权威 PASS=100/双跑 digest 一致/错 CRS typed fail/双权威互斥）：PASS
- test_teaching_foundation_e2e（shipped labs 编译校验+foundry artifact 门控会话完成）：PASS
- 本轨新增 CT-D1 契约守卫（test_r25_e_repo_flow_contract 2 用例）：PASS

## NOT_EXECUTED(reason)
- Windows/MSVC 实机 cmd.exe 腿：无实机；run_lab.cmd 改动以脚本内容契约测试锁定（--lab ndvi_landsat 出现且 ndvi_basics 消失），且数据文件存在性经 CT-D1 守卫验证
- 远端 CI/CD：协议约定不等待
- macOS：无实机（本轨为 Qt 无关后端 + 1 个 Qt dock 改动的注释级重述，dock 冒烟测试 Linux offscreen 绿）

## 修复中过程记录
- 首轮构建失败 ×2：测试代码笔误（临时对象指针循环、ref 解引用残留）→ 修正提交；无生产代码错误
- discovery_cancel 首跑 2 用例失败：测试 fixture 错（skip 向量别名复用、根级 feedback 名按 master 语义本就不拒收）→ fixture 修正后 4/4
